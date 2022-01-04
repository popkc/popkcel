/*
Copyright (C) 2020-2022 popkc(popkc at 163 dot com)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "popkcel.h"

struct Popkcel_Rbtnode* popkcel_rbtFind(struct Popkcel_Rbtnode* root, int64_t key)
{
    int64_t r;
    while (root) {
        r = key - root->key;
        if (r == 0) {
            while (root->left && root->left->key == key) {
                root = root->left;
            }
            return root;
        }
        else if (r < 0) {
            root = root->left;
        }
        else
            root = root->right;
    }
    return NULL;
}

static void rightRotate(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* node)
{
    struct Popkcel_Rbtnode* left = node->left;
    node->left = left->right;
    if (node->left)
        node->left->parent = node;
    left->parent = node->parent;
    if (!left->parent) {
        *root = left;
    }
    else {
        if (node == node->parent->left)
            node->parent->left = left;
        else
            node->parent->right = left;
    }
    left->right = node;
    node->parent = left;
}

static void leftRotate(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* node)
{
    struct Popkcel_Rbtnode* right = node->right;
    node->right = right->left;
    if (node->right)
        node->right->parent = node;
    right->parent = node->parent;
    if (!right->parent) {
        *root = right;
    }
    else {
        if (node == node->parent->left)
            node->parent->left = right;
        else
            node->parent->right = right;
    }
    right->left = node;
    node->parent = right;
}

static void rbtBalance(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* node)
{
    struct Popkcel_Rbtnode* pnode;
    while ((pnode = node->parent) && pnode->isRed) {
        struct Popkcel_Rbtnode* gpnode = pnode->parent;
        if (gpnode->left == pnode) {
            if (gpnode->right && gpnode->right->isRed) {
                pnode->isRed = 0;
                gpnode->right->isRed = 0;
                gpnode->isRed = 1;
                node = gpnode;
            }
            else {
                if (node == pnode->right) {
                    struct Popkcel_Rbtnode* tmp;
                    leftRotate(root, pnode);
                    tmp = node;
                    node = pnode;
                    pnode = tmp;
                }

                pnode->isRed = 0;
                gpnode->isRed = 1;
                rightRotate(root, gpnode);
            }
        }
        else {
            if (gpnode->left && gpnode->left->isRed) {
                pnode->isRed = 0;
                gpnode->left->isRed = 0;
                gpnode->isRed = 1;
                node = gpnode;
            }
            else {
                if (node == pnode->left) {
                    struct Popkcel_Rbtnode* tmp;
                    rightRotate(root, pnode);
                    tmp = node;
                    node = pnode;
                    pnode = tmp;
                }

                pnode->isRed = 0;
                gpnode->isRed = 1;
                leftRotate(root, gpnode);
            }
        }
    }

    (*root)->isRed = 0;
}

struct Popkcel_RbtInsertPos popkcel_rbtInsertPos(struct Popkcel_Rbtnode** root, int64_t key)
{
    struct Popkcel_RbtInsertPos ipos;
    if (!*root) {
        ipos.ipos = root;
        ipos.parent = NULL;
        return ipos;
    }
    struct Popkcel_Rbtnode* pnode;
    do {
        int64_t r = key - (*root)->key;
        if (r == 0) {
            ipos.ipos = NULL;
            ipos.parent = *root;
            return ipos;
        }
        else {
            pnode = *root;
            if (r < 0)
                root = &pnode->left;
            else
                root = &pnode->right;
        }
    } while (*root);
    ipos.ipos = root;
    ipos.parent = pnode;
    return ipos;
}

void popkcel_rbtInsertAtPos(struct Popkcel_Rbtnode** root, struct Popkcel_RbtInsertPos ipos, struct Popkcel_Rbtnode* inode)
{
    *ipos.ipos = inode;
    inode->left = NULL;
    inode->right = NULL;
    inode->parent = ipos.parent;
    if (ipos.parent) {
        inode->isRed = 1;
        rbtBalance(root, inode);
    }
    else
        inode->isRed = 0;
}

void popkcel_rbtMultiInsert(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* inode)
{
    if (!*root) {
        *root = inode;
        inode->left = NULL;
        inode->right = NULL;
        inode->parent = NULL;
        inode->isRed = 0;
    }
    else {
        struct Popkcel_Rbtnode** anode = root;
        struct Popkcel_Rbtnode* pnode;
        do {
            int64_t r = inode->key - (*anode)->key;
            pnode = *anode;
            if (r < 0)
                anode = &pnode->left;
            else
                anode = &pnode->right;
        } while (*anode);
        *anode = inode;
        inode->parent = pnode;
        inode->isRed = 1;
        inode->left = NULL;
        inode->right = NULL;
        rbtBalance(root, inode);
    }
}

struct Popkcel_Rbtnode* popkcel_rbtNext(struct Popkcel_Rbtnode* node)
{
    struct Popkcel_Rbtnode* n = node->right;
    if (n) {
        while (n->left) {
            n = n->left;
        }
        return n;
    }

    while (node->parent) {
        if (node == node->parent->left)
            return node->parent;
        else
            node = node->parent;
    }
    return NULL;
}

struct Popkcel_Rbtnode* popkcel_rbtBegin(struct Popkcel_Rbtnode* root)
{
    if (!root)
        return NULL;
    while (root->left)
        root = root->left;
    return root;
}

void popkcel_rbtDelete(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* node)
{
    struct Popkcel_Rbtnode* n2,*child,*parent;
    char isRed;
    if (node->left && node->right) {
        n2 = node->right;
        while (n2->left) {
            n2 = n2->left;
        }

        if (node->parent) {
            if (node == node->parent->left)
                node->parent->left = n2;
            else
                node->parent->right = n2;
        }
        else
            *root = n2;

        child = n2->right;
        parent = n2->parent;
        isRed = n2->isRed;

        if (parent == node) {
            parent = n2;
        }
        else {
            if (child)
                child->parent = parent;
            parent->left = child;

            n2->right = node->right;
            node->right->parent = n2;
        }

        n2->parent = node->parent;
        n2->left = node->left;
        n2->isRed = node->isRed;
        node->left->parent = n2;
    }
    else {
        if (node->left)
            child = node->left;
        else
            child = node->right;

        parent = node->parent;
        isRed = node->isRed;

        if (child)
            child->parent = node->parent;

        if (parent) {
            if (parent->left == node)
                parent->left = child;
            else
                parent->right = child;
        }
        else
            *root = child;
    }

    if (!isRed) {
        while ((!child || !child->isRed) && child != *root) {
            if (parent->left == child) {
                n2 = parent->right;
                if (n2->isRed) {
                    n2->isRed = 0;
                    parent->isRed = 1;
                    leftRotate(root, parent);
                    n2 = parent->right;
                }

                if ((!n2->left || !n2->left->isRed) && (!n2->right || !n2->right->isRed)) {
                    n2->isRed = 1;
                    child = parent;
                    parent = child->parent;
                }
                else {
                    if (!n2->right || !n2->right->isRed) {
                        n2->left->isRed = 0;
                        n2->isRed = 1;
                        rightRotate(root, n2);
                        n2 = parent->right;
                    }

                    n2->isRed = parent->isRed;
                    parent->isRed = 0;
                    n2->right->isRed = 0;
                    leftRotate(root, parent);
                    child = *root;
                    break;
                }
            }
            else {
                n2 = parent->left;
                if (n2->isRed) {
                    n2->isRed = 0;
                    parent->isRed = 1;
                    rightRotate(root, parent);
                    n2 = parent->left;
                }

                if ((!n2->left || !n2->left->isRed) && (!n2->right || !n2->right->isRed)) {
                    n2->isRed = 1;
                    child = parent;
                    parent = child->parent;
                }
                else {
                    if (!n2->left || !n2->left->isRed) {
                        n2->right->isRed = 0;
                        n2->isRed = 1;
                        leftRotate(root, n2);
                        n2 = parent->left;
                    }

                    n2->isRed = parent->isRed;
                    parent->isRed = 0;
                    n2->left->isRed = 0;
                    rightRotate(root, parent);
                    child = *root;
                    break;
                }
            }
        }

        if (child)
            child->isRed = 0;
    }
}
/*
void popkcel_rbtDelete(struct Popkcel_Rbtnode** root, struct Popkcel_Rbtnode* node)
{
    struct Popkcel_Rbtnode* n2;
restart:;
    if (!node->left && !node->right) {
        if (node->parent) {
            if (node->parent->left == node)
                node->parent->left = NULL;
            else
                node->parent->right = NULL;
        }
        else {
            *root = NULL;
            return;
        }

        n2 = NULL;
    }
    else if (node->left && node->right) {
        n2 = node->right;
        while (n2->left) {
            n2 = n2->left;
        }

        node->left->parent = n2;
        node->right->parent = n2;
        if (node->parent) {
            if (node->parent->left == node)
                node->parent->left = n2;
            else
                node->parent->right = n2;
        }
        else
            *root = n2;
        struct Popkcel_Rbtnode trbt = *n2;
        n2->left = node->left;
        n2->right = node->right;
        n2->parent = node->parent;
        n2->isRed = node->isRed;

        node->left = trbt.left;
        node->right = trbt.right;
        node->parent = trbt.parent;
        node->isRed = trbt.isRed;
        if (node->parent->left == n2)
            node->parent->left = node;
        else
            node->parent->right = node;
        if (node->right)
            node->right->parent = node;
        goto restart;
    }
    else {
        if (node->left)
            n2 = node->left;
        else
            n2 = node->right;

        if (node->parent) {
            if (node->parent->left == node)
                node->parent->left = n2;
            else
                node->parent->right = n2;
        }
        else {
            *root = n2;
        }

        n2->isRed = 0;
        n2->parent = node->parent;
    }

    if (!node->isRed) {
        struct Popkcel_Rbtnode *n3, *pnode = node->parent;

        while ((!n2 || !n2->isRed) && pnode) {
            n3 = pnode->right;
            if (n3 != n2) {
                if (n3->isRed) {
                    n3->isRed = 0;
                    pnode->isRed = 1;
                    leftRotate(root, pnode);
                    n3 = pnode->right;
                }

                if ((!n3->left || !n3->left->isRed) && (!n3->right || !n3->right->isRed)) {
                    n3->isRed = 1;
                    n2 = pnode;
                    pnode = pnode->parent;
                }
                else {
                    if (!n3->right || !n3->right->isRed) {
                        n3->left->isRed = 0;
                        n3->isRed = 1;
                        rightRotate(root, n3);
                        n3 = pnode->right;
                    }

                    n3->isRed = pnode->isRed;
                    pnode->isRed = 0;
                    n3->right->isRed = 0;
                    leftRotate(root, pnode);
                    n2 = *root;
                    break;
                }
            }
            else {
                n3 = pnode->left;
                if (n3->isRed) {
                    n3->isRed = 0;
                    pnode->isRed = 1;
                    rightRotate(root, pnode);
                    n3 = pnode->left;
                }

                if ((!n3->left || !n3->left->isRed) && (!n3->right || !n3->right->isRed)) {
                    n3->isRed = 1;
                    n2 = pnode;
                    pnode = pnode->parent;
                }
                else {
                    if (!n3->left || !n3->left->isRed) {
                        n3->right->isRed = 0;
                        n3->isRed = 1;
                        leftRotate(root, n3);
                        n3 = pnode->left;
                    }

                    n3->isRed = pnode->isRed;
                    pnode->isRed = 0;
                    n3->left->isRed = 0;
                    rightRotate(root, pnode);
                    n2 = *root;
                    break;
                }
            }
        }

        if (n2)
            n2->isRed = 0;
    }
}
*/
struct Popkcel_Rbtnode* popkcel_rbtLowerBound(struct Popkcel_Rbtnode* root, int64_t key)
{
    struct Popkcel_Rbtnode* it = NULL;
    while (root) {
        if (key < root->key) {
            it = root;
            root = root->left;
        }
        else if (key == root->key) {
            while (root->left && root->left->key == key)
                root = root->left;
            return root;
        }
        else {
            root = root->right;
        }
    }
    return it;
}
