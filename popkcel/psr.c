/*
Copyright (C) 2020-2022 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include "popkcel.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sendCb(void* data, intptr_t rv);
static int rpsSend(struct Popkcel_RbtnodePsrSend* rps);
static int psrTimerCb(void* data, intptr_t rv);

static int rpsInit(struct Popkcel_RbtnodePsrSend* rps, struct Popkcel_PsrField* psr)
{
    rps->sendCount = 0;
    rps->psr = psr;
    rps->callback = NULL;
    popkcel_initTimer(&rps->timer, psr->sock->loop);
    rps->timer.funcCb = &psrTimerCb;
    rps->timer.cbData = rps;
    int r = rpsSend(rps);
    if (r == POPKCEL_ERROR)
        return POPKCEL_ERROR;
    else if (r >= 0)
        popkcel_setTimer(&rps->timer, 5000, 5000);
    popkcel_rbtMultiInsert(&psr->nodePieceSend, (struct Popkcel_Rbtnode*)rps);
    return r;
}

static void psrError(struct Popkcel_PsrField* psr)
{
    popkcel_destroyPsrField(psr);
    if (psr->callback) {
        psr->callback(psr, POPKCEL_ERROR);
    }
}

static int canSend(struct Popkcel_PsrField* psr, uint32_t sid)
{
    uint32_t rlid = psr->lastMyConfirmedSendId;
    struct Popkcel_Rbtnode* it = popkcel_rbtBegin(psr->nodePieceSend);
    if (it && it->key < rlid)
        rlid = (uint32_t)it->key - 1;
    uint32_t e = rlid + psr->window + 1;
    return sid >= rlid && sid <= e;
}

static void psrCheckUnsent(struct Popkcel_PsrField* psr) //效率有点低，可优化
{
    struct Popkcel_Rbtnode* it = popkcel_rbtBegin(psr->nodePieceSend);
    if (!it)
        return;

    uint32_t rlid = psr->lastMyConfirmedSendId;
    if (it->key < rlid && (rlid < UINT32_MAX - psr->window || it->key > psr->window))
        rlid = (uint32_t)it->key - 1;
    uint32_t e = rlid + psr->window + 1;

    do {
        struct Popkcel_RbtnodePsrSend* rps = (struct Popkcel_RbtnodePsrSend*)it;
        if (rps->sendCount == -1) {
            if (rps->key >= rlid && rps->key <= e) {
                rps->sendCount = 0;
                if (rpsSend(rps) < 0)
                    return;
            }
        }
        it = popkcel_rbtNext(it);
    } while (it);
    psr->needSend = 0;
}

static uint32_t bufChecksum(const char* tranId, const char* buf, int bufLen)
{
    uint32_t ul;
    memcpy(&ul, tranId, 4);
    for (int i = 0; i < bufLen; i += 4) {
        uint32_t ul2;
        if (bufLen - i < 4) {
            ul2 = 0;
            memcpy(&ul2, buf + i, bufLen - i);
        }
        else
            memcpy(&ul2, buf + i, 4);
        ul ^= ul2;
    }
    return ul;
}

static int64_t ipPortHash(struct sockaddr* addr)
{
    uint64_t r = 0;
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in* a4 = (struct sockaddr_in*)addr;
        memcpy((char*)&r + 2, &a4->sin_addr, 4);
        memcpy(&r, &a4->sin_port, 2);
    }
    else {
        struct sockaddr_in6* a6 = (struct sockaddr_in6*)addr;
        char* i6 = (char*)&a6->sin6_addr;
        for (size_t i = 0; i < 16; i++) {
            r = i6[i] + (r << 6) + (r << 16) - r;
        }
        memcpy(&r, &a6->sin6_port, 2);
        r |= 1;
    }
    return r;
}

static void psrAddReply(struct Popkcel_PsrField* psr, uint32_t rid)
{
    struct Popkcel_RbtInsertPos ipos = popkcel_rbtInsertPos(&psr->nodeReply, rid);
    if (ipos.ipos) {
        struct Popkcel_Rbtnode* it = malloc(sizeof(struct Popkcel_Rbtnode));
        it->key = rid;
        popkcel_rbtInsertAtPos(&psr->nodeReply, ipos, it);
        if (!psr->timerStarted) {
            popkcel_setTimer(&psr->timer, 10, 0);
            psr->timerStarted = 1;
        }
    }
}

static void sendConnConfirm(struct Popkcel_PsrField* psr)
{
    psr->mySendId++;
    if (psr->mySendId >= 4) {
        psrError(psr);
        return;
    }
    unsigned char buf[7];
    buf[0] = POPKCEL_PF_APT | POPKCEL_PF_SYN | POPKCEL_PF_REPLY;
    memcpy(buf + 1, psr->tranId, 4);
    uint16_t us = htons(psr->window);
    memcpy(buf + 5, &us, 2);
    popkcel_trySendto((struct Popkcel_Socket*)psr->sock, (char*)buf, 7, (struct sockaddr*)&psr->remoteAddr, psr->addrLen, NULL, NULL);
}

static void sendSynConfirmReply(struct Popkcel_PsrField* psr)
{
    psr->synConfirm = 1;
    memcpy(psr->tranIdNew, psr->sock->psrBuffer + 2, 4);
    unsigned char buf[9];
    buf[0] = POPKCEL_PF_APT | POPKCEL_PF_SYN | POPKCEL_PF_REPLY | POPKCEL_PF_CONFIRM;
    memcpy(buf + 1, psr->tranIdNew, 4);
    memcpy(buf + 5, psr->tranId, 4);
    popkcel_trySendto((struct Popkcel_Socket*)psr->sock, (char*)buf, 9, (struct sockaddr*)&psr->remoteAddr, psr->addrLen, NULL, NULL);
}

static void createNewConnection(struct Popkcel_PsrSocket* sock, uint16_t us, int64_t h)
{
    us = ntohs(us);
    if (us <= sock->maxWindow)
        sock->psrWindow = us;
    else
        sock->psrWindow = sock->maxWindow;
    struct Popkcel_PsrField* psr = sock->listenCb(sock);
    if (psr) {
        sendConnConfirm(psr);
        struct Popkcel_RbtnodeData* it = malloc(sizeof(struct Popkcel_RbtnodeData));
        it->key = h;
        it->value = psr;
        popkcel_rbtMultiInsert(&sock->nodePf, (struct Popkcel_Rbtnode*)it);
    }
}

static int psrRecvFromCb(void* data, intptr_t rv)
{
    struct Popkcel_PsrSocket* sock = data;
    struct Popkcel_PsrField* psr;
    int64_t h;
    uint32_t ul, ul2;
    uint16_t us;
    unsigned char flag;
    //printf("recv %lld\n", rv);
restart:;
    if (rv == POPKCEL_ERROR) {
        //todo
    }

    if (rv > POPKCEL_MAXUDPSIZE)
        goto end;

    if (sock->recvCb) {
        int r = sock->recvCb(sock, rv);
        if (r == 1)
            goto end;
        else if (r == 2)
            return 0;
    }

    if (rv < 5)
        goto end;
    flag = sock->psrBuffer[0];
    if (!(flag & POPKCEL_PF_APT))
        goto end;
    flag &= 0x7f;
    if (flag == POPKCEL_PF_SYN) {
        if (sock->listenCb) {
            if (rv != 8)
                goto end;
            if (sock->psrBuffer[1] != POPKCEL_PSRVERSION)
                goto end;

            h = ipPortHash((struct sockaddr*)&sock->remoteAddr);
            struct Popkcel_RbtnodeData* it = (struct Popkcel_RbtnodeData*)popkcel_rbtFind(sock->nodePf, h);
            if (it) {
                psr = it->value;
                if (sock->ipv6) {
                    while (memcmp(&psr->remoteAddr.sin6_addr, &sock->remoteAddr.sin6_addr, sizeof(sock->remoteAddr.sin6_addr))) {
                        it = (struct Popkcel_RbtnodeData*)popkcel_rbtNext((struct Popkcel_Rbtnode*)it);
                        if (!it || it->key != h)
                            goto passCheck;
                        psr = it->value;
                    }
                }

                if (psr->state == POPKCEL_PS_CONNECTED) {
                    memcpy(sock->tranId, sock->psrBuffer + 2, 4);
                    memcpy(&us, sock->psrBuffer + 6, 2);
                    us = ntohs(us);
                    if (us <= sock->maxWindow)
                        psr->window = us;
                    else
                        psr->window = sock->maxWindow;
                    sendConnConfirm(psr);
                }
                else if (psr->state == POPKCEL_PS_TRANSFER) {
                    sendSynConfirmReply(psr);
                }
                goto end;
            }
        passCheck:;
            memcpy(sock->tranId, sock->psrBuffer + 2, 4);
            memcpy(&us, sock->psrBuffer + 6, 2);
            createNewConnection(sock, us, h);
        }
    }
    else {
        h = ipPortHash((struct sockaddr*)&sock->remoteAddr);
        struct Popkcel_RbtnodeData* it = (struct Popkcel_RbtnodeData*)popkcel_rbtFind(sock->nodePf, h);
        if (it) {
            psr = it->value;
            if (sock->ipv6) {
                while (memcmp(&psr->remoteAddr.sin6_addr, &sock->remoteAddr.sin6_addr, sizeof(sock->remoteAddr.sin6_addr))) {
                    it = (struct Popkcel_RbtnodeData*)popkcel_rbtNext((struct Popkcel_Rbtnode*)it);
                    if (!it || it->key != h)
                        goto end;
                    psr = it->value;
                }
            }

            if (flag & POPKCEL_PF_SYN) {
                switch (flag) {
                case POPKCEL_PF_SYN | POPKCEL_PF_REPLY: {
                    if (psr->state != POPKCEL_PS_CONNECTING)
                        goto end;
                    if (rv != 7)
                        goto end;
                    if (memcmp(psr->tranId, sock->psrBuffer + 1, 4))
                        goto end;
                    memcpy(&us, sock->psrBuffer + 5, 2);
                    us = ntohs(us);
                    if (us > psr->window)
                        goto end;
                    psr->mySendId = 0;
                    psr->synConfirm = 0;
                    psr->state = POPKCEL_PS_TRANSFER;
                    psr->window = us;
                    if (psr->nodePieceSend) {
                        struct Popkcel_RbtnodePsrSend* it = (struct Popkcel_RbtnodePsrSend*)psr->nodePieceSend;
                        popkcel_stopTimer(&it->timer);
                        psr->nodePieceSend = NULL;
                        if (it->pending == 0)
                            free(it);
                        else
                            it->pending = 2;
                    }
                    if (psr->callback) {
                        psr->callback(psr, POPKCEL_CONNECTED);
                    }
                } break;
                case POPKCEL_PF_SYN | POPKCEL_PF_CONFIRM: {
                    if (!sock->listenCb)
                        goto end;
                    if (psr->state != POPKCEL_PS_TRANSFER)
                        goto end;
                    if (!psr->synConfirm)
                        goto end;
                    if (rv != 11)
                        goto end;
                    if (memcmp(psr->tranId, sock->psrBuffer + 5, 4))
                        goto end;
                    if (memcmp(psr->tranIdNew, sock->psrBuffer + 1, 4))
                        goto end;
                    psrError(psr);
                    memcpy(sock->tranId, sock->psrBuffer + 1, 4);
                    memcpy(&us, sock->psrBuffer + 9, 2);
                    createNewConnection(sock, us, h);
                } break;
                case POPKCEL_PF_SYN | POPKCEL_PF_REPLY | POPKCEL_PF_CONFIRM: {
                    if (psr->state != POPKCEL_PS_CONNECTING)
                        goto end;
                    if (psr->synConfirm)
                        goto end;
                    if (rv != 9)
                        goto end;
                    if (memcmp(sock->psrBuffer + 1, psr->tranId, 4))
                        goto end;
                    psr->synConfirm = 1;
                    if (psr->nodePieceSend) {
                        struct Popkcel_RbtnodePsrSend* it = (struct Popkcel_RbtnodePsrSend*)psr->nodePieceSend;
                        popkcel_stopTimer(&it->timer);
                        psr->nodePieceSend = NULL;
                        if (it->pending == 0)
                            free(it);
                        else
                            it->pending = 2;
                    }
                    struct Popkcel_RbtnodePsrSend* rps = malloc(sizeof(struct Popkcel_RbtnodePsrSend) + 11);
                    rps->key = 0;
                    rps->bufLen = 11;
                    rps->buffer[0] = (unsigned char)(POPKCEL_PF_APT | POPKCEL_PF_SYN | POPKCEL_PF_CONFIRM);
                    memcpy(rps->buffer + 1, psr->tranId, 4);
                    memcpy(rps->buffer + 5, sock->psrBuffer + 5, 4);
                    us = htons(psr->window);
                    memcpy(rps->buffer + 9, &us, 2);
                    if (rpsInit(rps, psr) == POPKCEL_ERROR) {
                        free(rps);
                        goto end;
                    }
                } break;
                default:
                    break;
                }
            }
            else if (flag & POPKCEL_PF_TRANSFORM) {
                if (flag & 0x78)
                    goto end;
                if (psr->state == POPKCEL_PS_CONNECTED) {
                    psr->mySendId = 0;
                    psr->state = POPKCEL_PS_TRANSFER;
                }
                else if (psr->state != POPKCEL_PS_TRANSFER)
                    goto end;

                ul = bufChecksum(psr->tranId, sock->psrBuffer + 5, (int)rv - 5);
                if (memcmp(&ul, sock->psrBuffer + 1, 4))
                    goto end; //checksum fail

                ul = 5;
                do {
                    switch (flag) {
                    case POPKCEL_PF_TRANSFORM:
                        if (rv < ul + 6)
                            goto end;
                        memcpy(&ul2, sock->psrBuffer + ul, 4);
                        ul2 = ntohl(ul2);
                        ul += 4;
                        memcpy(&us, sock->psrBuffer + ul, 2);
                        us = ntohs(us);
                        ul += 2;
                        if (!us || ul + us > rv)
                            goto end;
                        psrAddReply(psr, ul2);
                        if (ul2 == psr->othersSendId) {
                            psr->recvBuf = sock->psrBuffer + ul;
                            psr->othersSendId++;
                            if (psr->callback(psr, us))
                                goto end;
                            struct Popkcel_Rbtnode* it = popkcel_rbtFind(psr->nodePieceReceive, psr->othersSendId);
                            if (it) {
                                do {
                                    struct Popkcel_RbtnodeBuf* rit = (struct Popkcel_RbtnodeBuf*)it;
                                    psr->recvBuf = rit->buffer;
                                    if (psr->callback(psr, rit->bufLen))
                                        goto end;
                                    psr->othersSendId++;
                                    if (psr->othersSendId == 0) {
                                        popkcel_rbtDelete(&psr->nodePieceReceive, (struct Popkcel_Rbtnode*)rit);
                                        free(rit);
                                        it = popkcel_rbtBegin(psr->nodePieceReceive);
                                    }
                                    else {
                                        it = popkcel_rbtNext(it);
                                        popkcel_rbtDelete(&psr->nodePieceReceive, (struct Popkcel_Rbtnode*)rit);
                                        free(rit);
                                    }
                                } while (it && it->key == psr->othersSendId);
                            }
                        }
                        else {
                            uint32_t m = psr->othersSendId + psr->window;
                            if (ul2 <= m || (m < psr->othersSendId && ul2 > psr->othersSendId)) {
                                struct Popkcel_RbtInsertPos ipos = popkcel_rbtInsertPos(&psr->nodePieceReceive, ul2);
                                if (ipos.ipos) {
                                    struct Popkcel_RbtnodeBuf* rb = malloc(sizeof(struct Popkcel_RbtnodeBuf) + us);
                                    rb->bufLen = us;
                                    rb->key = ul2;
                                    memcpy(rb->buffer, sock->psrBuffer + ul, us);
                                    popkcel_rbtInsertAtPos(&psr->nodePieceReceive, ipos, (struct Popkcel_Rbtnode*)rb);
                                }
                            }
                        }
                        ul += us;
                        break;
                    case POPKCEL_PF_TRANSFORM | POPKCEL_PF_REPLY: {
                        if (rv < ul + 8)
                            goto end;
                        uint32_t st;
                        memcpy(&st, sock->psrBuffer + ul, 4);
                        st = ntohl(st);
                        ul += 4;
                        uint32_t e;
                        memcpy(&e, sock->psrBuffer + ul, 4);
                        e = ntohl(e);
                        ul += 4;
                        uint32_t len = e - st;
                        if (len > psr->window)
                            goto end;

                        struct Popkcel_Rbtnode* it = popkcel_rbtLowerBound(psr->nodePieceSend, st);
                        int restarted = 0;
                        while (it && it->key - st <= len) {
                            if (psr->lastMyConfirmedSendId < it->key
                                || (psr->lastMyConfirmedSendId >= UINT32_MAX - psr->window && it->key <= psr->window))
                                psr->lastMyConfirmedSendId = (uint32_t)it->key;
                            struct Popkcel_RbtnodePsrSend* sit = (struct Popkcel_RbtnodePsrSend*)it;
                            it = popkcel_rbtNext(it);
                            popkcel_stopTimer(&sit->timer);
                            popkcel_rbtDelete(&psr->nodePieceSend, (struct Popkcel_Rbtnode*)sit);
                            if (sit->pending == 0)
                                free(sit);
                            else
                                sit->pending = 2;
                            if (!it && e < st && !restarted) {
                                it = popkcel_rbtBegin(psr->nodePieceSend);
                                restarted = 1;
                            }
                        }
                        psrCheckUnsent(psr);
                    } break;
                    case POPKCEL_PF_TRANSFORM | POPKCEL_PF_REPLY | POPKCEL_PF_SINGLE: {
                        if (rv < ul + 5)
                            goto end;
                        size_t count = (unsigned char)sock->psrBuffer[ul];
                        ul++;
                        if (count == 0 || (size_t)rv < ul + count * 4)
                            goto end;
                        do {
                            memcpy(&ul2, sock->psrBuffer + ul, 4);
                            ul2 = ntohl(ul2);
                            struct Popkcel_RbtnodePsrSend* it = (struct Popkcel_RbtnodePsrSend*)popkcel_rbtFind(psr->nodePieceSend, ul2);
                            if (it) {
                                if (psr->lastMyConfirmedSendId < it->key
                                    || (psr->lastMyConfirmedSendId >= UINT32_MAX - psr->window && it->key <= psr->window))
                                    psr->lastMyConfirmedSendId = (uint32_t)it->key;
                                popkcel_stopTimer(&it->timer);
                                popkcel_rbtDelete(&psr->nodePieceSend, (struct Popkcel_Rbtnode*)it);
                                if (it->pending == 0)
                                    free(it);
                                else
                                    it->pending = 2;
                            }
                            count--;
                            ul += 4;
                        } while (count);
                        psrCheckUnsent(psr);
                    } break;
                    case POPKCEL_PF_CLOSED:
                        psrError(psr);
                        goto end;
                    default:
                        goto end;
                    }

                    if (ul >= rv)
                        break;
                    flag = sock->psrBuffer[ul];
                    ul++;
                } while (ul < rv);
            }
            else if (flag == POPKCEL_PF_CLOSED) {
                if (rv != 5)
                    goto end;
                if (memcmp(sock->psrBuffer + 1, psr->tranId, 4))
                    goto end;
                psrError(psr);
            }
        }
    }
end:;
    rv = popkcel_tryRecvfrom((struct Popkcel_Socket*)sock, sock->psrBuffer, POPKCEL_MAXUDPSIZE, (struct sockaddr*)&sock->remoteAddr, &sock->remoteAddrLen, &psrRecvFromCb, sock);
    if (rv != POPKCEL_WOULDBLOCK)
        goto restart;
    return 0;
}

static int psrKlTimerCb(void* data, intptr_t rv)
{
    struct Popkcel_PsrSocket* sock = data;
    if (sock->lastSendTime) {
        int64_t nt = popkcel_getCurrentTime();
        if (nt - sock->lastSendTime > 15000) {
            sock->lastSendTime = nt;
            if (!sock->ipv6) {
                popkcel_trySendto((struct Popkcel_Socket*)sock, (char*)sock, 1, (struct sockaddr*)&popkcel_globalVar->tempAddr, sizeof(struct sockaddr_in), NULL, NULL);
            }
            else {
                popkcel_trySendto((struct Popkcel_Socket*)sock, (char*)sock, 1, (struct sockaddr*)&popkcel_globalVar->tempAddr6, sizeof(struct sockaddr_in6), NULL, NULL);
            }
        }
    }
    return 0;
}

struct LbSingle
{
    uint32_t ids[255];
    uint32_t pos;
};

static int addSingle(struct LbSingle** ids, int* pbufLen, size_t pos, uint32_t id)
{
    if (!*ids) {
        *ids = malloc(sizeof(struct LbSingle));
        (*ids)->pos = 0;
        *pbufLen -= 2;
    }

    if (*pbufLen - pos >= 4) {
        (*ids)->ids[(*ids)->pos] = htonl(id);
        (*ids)->pos++;
        *pbufLen -= 4;
        if ((*ids)->pos < 255)
            return 0;
    }

    return 1;
}

static void addRange(uint32_t sid, uint32_t lid, char* buf, int* pos)
{
    buf[*pos] = (POPKCEL_PF_REPLY | POPKCEL_PF_TRANSFORM);
    (*pos)++;
    uint32_t ul = htonl(sid);
    memcpy(buf + *pos, &ul, 4);
    *pos += 4;
    ul = htonl(lid);
    memcpy(buf + *pos, &ul, 4);
    *pos += 4;
}

static int makeReplyBuffer(struct Popkcel_PsrField* psr, char* buf, int bufLen)
{
    int pos = 0;
    struct LbSingle* ids = NULL;
    int64_t sid = -1, lid;
    struct Popkcel_Rbtnode* it = popkcel_rbtBegin(psr->nodeReply);
    while (it) {
        if (sid >= 0) {
            if (it->key == lid + 1) {
                lid++;
                struct Popkcel_Rbtnode* oit = it;
                it = popkcel_rbtNext(it);
                popkcel_rbtDelete(&psr->nodeReply, oit);
                free(oit);
                continue;
            }
            else if (sid == lid) {
                if (addSingle(&ids, &bufLen, pos, (uint32_t)sid))
                    break;
            }
            else {
                addRange((uint32_t)sid, (uint32_t)lid, buf, &pos);
            }
        }

        int remain = bufLen - pos;
        if (remain < 4)
            break;
        else if (remain < 9) {
            if (remain < 6 && !ids)
                break;
            addSingle(&ids, &bufLen, pos, (uint32_t)it->key);
            popkcel_rbtDelete(&psr->nodeReply, it);
            free(it);
            break;
        }
        else {
            sid = lid = it->key;
            struct Popkcel_Rbtnode* oit = it;
            it = popkcel_rbtNext(it);
            popkcel_rbtDelete(&psr->nodeReply, oit);
            free(oit);
        }
    }

    if (sid >= 0 && !it) { //遍历完成
        if (sid == lid) {
            if (ids)
                addSingle(&ids, &bufLen, pos, (uint32_t)sid);
            else { //最常见的情况，所以单独写一下
                buf[pos] = (POPKCEL_PF_REPLY | POPKCEL_PF_TRANSFORM | POPKCEL_PF_SINGLE);
                pos++;
                buf[pos] = 1;
                pos++;
                uint32_t ul = htonl((uint32_t)sid);
                memcpy(buf + pos, &ul, 4);
                pos += 4;
            }
        }
        else
            addRange((uint32_t)sid, (uint32_t)lid, buf, &pos);
    }

    if (ids) {
        buf[pos] = (POPKCEL_PF_REPLY | POPKCEL_PF_TRANSFORM | POPKCEL_PF_SINGLE);
        pos++;
        buf[pos] = (unsigned char)pos;
        pos++;
        memcpy(buf + pos, ids->ids, 4 * ids->pos);
        pos += 4 * ids->pos;
        free(ids);
    }
    return pos;
}

static int rpsSend(struct Popkcel_RbtnodePsrSend* rps)
{
    rps->pending = 0;
    ssize_t r = popkcel_trySendto((struct Popkcel_Socket*)rps->psr->sock, rps->buffer, rps->bufLen, (struct sockaddr*)&rps->psr->remoteAddr, rps->psr->addrLen, &sendCb, rps);

    if (r == POPKCEL_ERROR) {
        popkcel_stopTimer(&rps->timer);
        psrError(rps->psr);
    }
    else if (r >= 0) {
        rps->psr->sock->lastSendTime = popkcel_getCurrentTime();
        rps->sendCount++;
    }
    else if (r == POPKCEL_WOULDBLOCK)
        rps->pending = 1;
    return (int)r;
}

static int sendCb(void* data, intptr_t rv)
{
    struct Popkcel_RbtnodePsrSend* rps = data;
    if (rps->pending == 2) {
        free(rps);
        return 0;
    }
    rps->pending = 0;

    if (rv == POPKCEL_ERROR) {
        popkcel_stopTimer(&rps->timer);
        psrError(rps->psr);
    }
    else {
        rps->psr->sock->lastSendTime = popkcel_getCurrentTime();
        rps->sendCount++;
        if (rps->sendCount == 1) {
            popkcel_setTimer(&rps->timer, 5000, 5000);
            if (rps->callback)
                rps->callback(rps->userData, POPKCEL_OK);
        }
    }
    return 0;
}

//5000毫秒一次重传
static int psrTimerCb(void* data, intptr_t rv)
{
    struct Popkcel_RbtnodePsrSend* rps = data;
    struct Popkcel_PsrField* psr = rps->psr;
    if (rps->sendCount >= 3) {
        popkcel_stopTimer(&rps->timer);
        psrError(psr);
        return 0;
    }

    rpsSend(rps);
    return 0;
}

static int psrSendBuffer(struct Popkcel_PsrField* psr, Popkcel_FuncCallback cb, void* data, int cs)
{
    assert(psr->bufferPos);
    uint32_t ul = bufChecksum(psr->tranId, psr->buffer + 5, psr->bufferPos - 5);
    memcpy(psr->buffer + 1, &ul, 4);

    struct Popkcel_RbtnodePsrSend* rps = malloc(sizeof(struct Popkcel_RbtnodePsrSend) + psr->bufferPos);
    rps->bufLen = psr->bufferPos;
    memcpy(rps->buffer, psr->buffer, psr->bufferPos);
    psr->bufferPos = 0;
    rps->psr = psr;
    rps->key = psr->mySendId;
    rps->timer.cbData = rps;
    rps->timer.funcCb = &psrTimerCb;
    popkcel_rbtMultiInsert(&psr->nodePieceSend, (struct Popkcel_Rbtnode*)rps);
    popkcel_initTimer(&rps->timer, psr->sock->loop);
    psr->mySendId++;
    rps->userData = data;
    rps->callback = cb;
    rps->sendCount = 0;

    if (!cs) {
        rps->sendCount = -1;
        return POPKCEL_WOULDBLOCK;
    }
    else {
        int r = rpsSend(rps);
        if (r == POPKCEL_ERROR) {
            free(rps);
            psrError(psr);
            return POPKCEL_ERROR;
        }

        if (r >= 0) {
            popkcel_setTimer(&rps->timer, 5000, 5000);
        }
        return r;
    }
}

//每10毫秒发送一次的timer cb，用于合并短的发送包
static int cbPsrTimerSend(void* data, intptr_t rv)
{
    struct Popkcel_PsrField* psr = data;
    do {
        char replyOnly;
        if (psr->bufferPos) {
            uint16_t us = htons(psr->bufferPos - 11);
            memcpy(psr->buffer + 9, &us, 2);
            replyOnly = 0;
        }
        else
            replyOnly = 1;

        if (psr->nodeReply && psr->bufferPos <= POPKCEL_MAXUDPSIZE - 9) {
            if (psr->bufferPos == 0) {
                int a = makeReplyBuffer(psr, psr->buffer + 4, POPKCEL_MAXUDPSIZE - 4);
                psr->bufferPos = a + 4;
                psr->buffer[0] = (psr->buffer[4] | POPKCEL_PF_APT);
            }
            else {
                int a = makeReplyBuffer(psr, psr->buffer + psr->bufferPos, POPKCEL_MAXUDPSIZE - psr->bufferPos);
                psr->bufferPos += a;
            }
        }

        if (replyOnly) {
            uint32_t ul = bufChecksum(psr->tranId, psr->buffer + 5, psr->bufferPos - 5);
            memcpy(psr->buffer + 1, &ul, 4);

            ssize_t r = popkcel_trySendto((struct Popkcel_Socket*)psr->sock, psr->buffer, psr->bufferPos, (struct sockaddr*)&psr->remoteAddr, psr->addrLen, NULL, NULL);
            psr->sock->lastSendTime = popkcel_getCurrentTime();
            if (r == POPKCEL_ERROR) {
                psrError(psr);
                return 0;
            }

            psr->bufferPos = 0;
        }
        else {
            if (psrSendBuffer(psr, psr->lastSendCallback, psr->lastSendUserData, 1) == POPKCEL_ERROR)
                return 0;
        }
    } while (psr->nodeReply);
    psr->timerStarted = 0;
    return 0;
}

int popkcel_psrTryConnect(struct Popkcel_PsrField* psr)
{
    if (psr->state != POPKCEL_PS_INIT)
        return POPKCEL_ERROR;
    int64_t h = ipPortHash((struct sockaddr*)&psr->remoteAddr);
    struct Popkcel_RbtnodeData* it;
    struct Popkcel_RbtInsertPos ipos;
    if (psr->sock->ipv6) {
        it = (struct Popkcel_RbtnodeData*)popkcel_rbtFind(psr->sock->nodePf, h);
        if (it) {
            do {
                struct Popkcel_PsrField* pf2 = it->value;
                if (!memcmp(&pf2->remoteAddr.sin6_addr, &psr->remoteAddr.sin6_addr, sizeof(psr->remoteAddr.sin6_addr)))
                    return POPKCEL_ERROR;
                it = (struct Popkcel_RbtnodeData*)popkcel_rbtNext((struct Popkcel_Rbtnode*)it);
            } while (it && it->key == h);
        }
    }
    else {
        ipos = popkcel_rbtInsertPos(&psr->sock->nodePf, h);
        if (!ipos.ipos)
            return POPKCEL_ERROR;
    }

    struct Popkcel_RbtnodePsrSend* rps = malloc(sizeof(struct Popkcel_RbtnodePsrSend) + 8);
    rps->key = 0;
    rps->bufLen = 8;

    uint32_t rnd = popkcel__rand();
    memcpy(psr->tranId, &rnd, 4);
    rps->buffer[0] = (unsigned char)(POPKCEL_PF_SYN | POPKCEL_PF_APT);
    rps->buffer[1] = POPKCEL_PSRVERSION;
    memcpy(rps->buffer + 2, &rnd, 4);
    uint16_t wnd = htons(psr->window);
    memcpy(rps->buffer + 6, &wnd, 2);
    if (rpsInit(rps, psr) == POPKCEL_ERROR) {
        free(rps);
        return POPKCEL_ERROR;
    }

    psr->state = POPKCEL_PS_CONNECTING;
    it = malloc(sizeof(struct Popkcel_RbtnodeData));
    it->key = h;
    it->value = psr;
    if (psr->sock->ipv6)
        popkcel_rbtMultiInsert(&psr->sock->nodePf, (struct Popkcel_Rbtnode*)it);
    else
        popkcel_rbtInsertAtPos(&psr->sock->nodePf, ipos, (struct Popkcel_Rbtnode*)it);
    return POPKCEL_WOULDBLOCK;
}

int popkcel_psrTrySend(struct Popkcel_PsrField* psr, const char* data, size_t len, Popkcel_FuncCallback callback, void* userData)
{
    //printf("psrTrySend %d\n", psr->state);
    if (psr->state == POPKCEL_PS_CONNECTED) {
        psr->state = POPKCEL_PS_TRANSFER;
        psr->mySendId = 0;
    }
    else if (psr->state != POPKCEL_PS_TRANSFER)
        return POPKCEL_ERROR;

    if (len == 0)
        return POPKCEL_OK;

    int cs = canSend(psr, psr->mySendId);
    int r;
    for (;;) {
        if (!psr->bufferPos) {
            psr->buffer[0] = (unsigned char)(POPKCEL_PF_TRANSFORM | POPKCEL_PF_APT);
            uint32_t ul = htonl(psr->mySendId);
            memcpy(psr->buffer + 5, &ul, 4);
            psr->bufferPos = 11;
        }

        size_t rlen = POPKCEL_MAXUDPSIZE - psr->bufferPos;
        if (rlen > len) {
            memcpy(psr->buffer + psr->bufferPos, data, len);
            psr->bufferPos += (uint32_t)len;
            r = (int)len;
            if (!cs) {
                psr->lastSendCallback = callback;
                psr->lastSendUserData = userData;
            }
            else {
                psr->lastSendCallback = NULL;
                if (!psr->timerStarted) {
                    popkcel_setTimer(&psr->timer, 10, 0);
                    psr->timerStarted = 1;
                }
            }
            break;
        }
        else {
            memcpy(psr->buffer + psr->bufferPos, data, rlen);
            uint32_t us = htons(POPKCEL_MAXUDPSIZE - 11);
            memcpy(psr->buffer + 9, &us, 2);
            psr->bufferPos = POPKCEL_MAXUDPSIZE;
            if (rlen == len) {
                r = psrSendBuffer(psr, callback, userData, cs);
                break;
            }
            else
                r = psrSendBuffer(psr, NULL, NULL, cs);

            if (r == POPKCEL_ERROR)
                return POPKCEL_ERROR;
            else if (r == POPKCEL_WOULDBLOCK)
                cs = 0;
            len -= rlen;
            data += rlen;
        }
    }

    if (r == POPKCEL_ERROR)
        return POPKCEL_ERROR;
    else if (!cs)
        return POPKCEL_WOULDBLOCK;
    else if (r >= 0)
        return (int)len;
    else
        return r;
}

void popkcel_psrAcceptOne(struct Popkcel_PsrSocket* sock, struct Popkcel_PsrField* psr, Popkcel_PsrFuncCallback cbFunc)
{
    popkcel_initPsrField(sock, psr, cbFunc);
    psr->state = POPKCEL_PS_CONNECTED;
    memcpy(psr->tranId, sock->tranId, 4);
    psr->window = sock->psrWindow;
    memcpy(&psr->remoteAddr, &sock->remoteAddr, sock->remoteAddrLen);
    psr->addrLen = sock->remoteAddrLen;
}

int popkcel_initPsrSocket(struct Popkcel_PsrSocket* sock, struct Popkcel_Loop* loop, Popkcel_HandleType fd, char ipv6, uint16_t port, Popkcel_PsrListenCb listenCb, Popkcel_PsrRecvCb recvCb, uint16_t maxWindow)
{
    int t;
    if (fd)
        t = POPKCEL_SOCKETTYPE_EXIST;
    else
        t = POPKCEL_SOCKETTYPE_UDP;
    if (ipv6)
        t |= POPKCEL_SOCKETTYPE_IPV6;
    if (popkcel_initSocket((struct Popkcel_Socket*)sock, loop, t, fd) == POPKCEL_ERROR)
        return POPKCEL_ERROR;
    if (popkcel_bind((struct Popkcel_Socket*)sock, port) == POPKCEL_ERROR) {
        popkcel_destroySocket((struct Popkcel_Socket*)sock);
        return POPKCEL_ERROR;
    }
    sock->listenCb = listenCb;
    sock->recvCb = recvCb;
    sock->maxWindow = maxWindow;
    sock->nodePf = NULL;
    sock->lastSendTime = 0;
    sock->remoteAddrLen = sizeof(sock->remoteAddr);

    ssize_t r = popkcel_tryRecvfrom((struct Popkcel_Socket*)sock, sock->psrBuffer, POPKCEL_MAXUDPSIZE, (struct sockaddr*)&sock->remoteAddr, &sock->remoteAddrLen, &psrRecvFromCb, sock);
    if (r >= 0)
        psrRecvFromCb(sock, r);
    else if (r == POPKCEL_ERROR) {
        popkcel_destroySocket((struct Popkcel_Socket*)sock);
        return POPKCEL_ERROR;
    }
    sock->timerKeepAlive.funcCb = &psrKlTimerCb;
    sock->timerKeepAlive.cbData = sock;
    popkcel_initTimer(&sock->timerKeepAlive, loop);
    popkcel_setTimer(&sock->timerKeepAlive, 15000, 15000);
    return POPKCEL_OK;
}

void popkcel_destroyPsrSocket(struct Popkcel_PsrSocket* sock)
{
    popkcel_stopTimer(&sock->timerKeepAlive);
    popkcel_destroySocket((struct Popkcel_Socket*)sock);
}

void popkcel_initPsrField(struct Popkcel_PsrSocket* sock, struct Popkcel_PsrField* psr, Popkcel_PsrFuncCallback cbFunc)
{
    psr->sock = sock;
    psr->nodePieceReceive = NULL;
    psr->nodePieceSend = NULL;
    psr->nodeReply = NULL;
    psr->state = POPKCEL_PS_INIT;
    psr->mySendId = 0;
    psr->othersSendId = 0;
    psr->lastMyConfirmedSendId = 0;
    psr->callback = cbFunc;
    psr->bufferPos = 0;
    psr->timer.cbData = psr;
    psr->timer.funcCb = &cbPsrTimerSend;
    psr->timerStarted = 0;
    psr->needSend = 0;
    psr->window = sock->maxWindow;
    psr->synConfirm = 0;
    popkcel_initTimer(&psr->timer, sock->loop);
    struct sockaddr* sa = (struct sockaddr*)&psr->remoteAddr;
    if (sock->ipv6) {
        sa->sa_family = AF_INET6;
        psr->addrLen = sizeof(struct sockaddr_in6);
    }
    else {
        sa->sa_family = AF_INET;
        psr->addrLen = sizeof(struct sockaddr_in);
    }
}

void popkcel_destroyPsrField(struct Popkcel_PsrField* psr)
{
    int64_t h = ipPortHash((struct sockaddr*)&psr->remoteAddr);
    struct Popkcel_Rbtnode* it = popkcel_rbtFind(psr->sock->nodePf, h);
    if (it) {
        if (psr->sock->ipv6) {
            while (((struct Popkcel_RbtnodeData*)it)->value != psr) {
                it = popkcel_rbtNext(it);
                if (!it || it->key != h)
                    goto notFound;
            }
        }
        popkcel_rbtDelete(&psr->sock->nodePf, it);
        free(it);
    }
notFound:;
    it = psr->nodePieceSend;
    while (it) {
        struct Popkcel_RbtnodePsrSend* oit = (struct Popkcel_RbtnodePsrSend*)it;
        if (it->left) {
            it = it->left;
            oit->left = NULL;
        }
        else if (it->right) {
            it = it->right;
            oit->right = NULL;
        }
        else {
            it = it->parent;
            popkcel_stopTimer(&oit->timer);
            if (oit->pending == 0)
                free(oit);
            else
                oit->pending = 2;
        }
    }
    psr->nodePieceSend = NULL;
    it = psr->nodePieceReceive;
    while (it) {
        struct Popkcel_Rbtnode* oit = it;
        if (it->left) {
            it = it->left;
            oit->left = NULL;
        }
        else if (it->right) {
            it = it->right;
            oit->right = NULL;
        }
        else {
            it = it->parent;
            free(oit);
        }
    }
    psr->nodePieceReceive = NULL;
    it = psr->nodeReply;
    while (it) {
        struct Popkcel_Rbtnode* oit = it;
        if (it->left) {
            it = it->left;
            oit->left = NULL;
        }
        else if (it->right) {
            it = it->right;
            oit->right = NULL;
        }
        else {
            it = it->parent;
            free(oit);
        }
    }
    psr->nodeReply = NULL;
    popkcel_stopTimer(&psr->timer);
}
