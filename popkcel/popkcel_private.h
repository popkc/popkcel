#ifndef POPKCEL_PRIVATE_H
#define POPKCEL_PRIVATE_H

#ifndef POPKCEL_NOFAKESYNC
#    ifndef STACKGROWTHUP
#        define ELCHECKIFONSTACK2(l, v, s) \
            if ((l)->running)              \
            assert(((char *)(v) < (char *)&sp || (char *)(v) > (char *)(l)->stackPos) && s)
#    else
#        define ELCHECKIFONSTACK2(l, v, s) \
            if ((l)->running)              \
            assert(((char *)(v) > (char *)&sp || (char *)(v) < (char *)(l)->stackPos) && s)
#    endif

#    ifdef NDEBUG
#        define ELCHECKIFONSTACK(l, v, s)
#    else
#        define ELCHECKIFONSTACK(l, v, s) \
            volatile char sp;             \
            ELCHECKIFONSTACK2(l, v, s)
#    endif
#else
#    define ELCHECKIFONSTACK(l, v, s)
#    define ELCHECKIFONSTACK2(l, v, s)
#endif

#ifdef _FORTIFY_SOURCE
#    undef _FORTIFY_SOURCE
#endif
#define _FORTIFY_SOURCE 0

#endif