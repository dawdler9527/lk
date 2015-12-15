/*
 * Copyright (c) 2008-2015 Travis Geiselbrecht
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <sys/io.h>

#include <ctype.h>
#include <debug.h>
#include <stdlib.h>
#include <printf.h>
#include <stdio.h>
#include <list.h>
#include <string.h>
#include <arch/ops.h>
#include <platform.h>
#include <platform/debug.h>
#include <kernel/thread.h>

#if WITH_LIB_SM
#define PRINT_LOCK_FLAGS SPIN_LOCK_FLAG_IRQ_FIQ
#else
#define PRINT_LOCK_FLAGS SPIN_LOCK_FLAG_INTERRUPTS
#endif

static spin_lock_t print_spin_lock = 0;
static struct list_node print_callbacks = LIST_INITIAL_VALUE(print_callbacks);

/* print lock must be held when invoking out, outs, outc */
static void out_count(const char *str, size_t len)
{
    print_callback_t *cb;
    size_t i;

    /* print to any registered loggers */
    if (!list_is_empty(&print_callbacks)) {
        spin_lock_saved_state_t state;
        spin_lock_save(&print_spin_lock, &state, PRINT_LOCK_FLAGS);

        list_for_every_entry(&print_callbacks, cb, print_callback_t, entry) {
            if (cb->print)
                cb->print(cb, str, len);
        }

        spin_unlock_restore(&print_spin_lock, state, PRINT_LOCK_FLAGS);
    }

    /* write out the serial port */
    for (i = 0; i < len; i++) {
        platform_dputc(str[i]);
    }
}

void register_print_callback(print_callback_t *cb)
{
    spin_lock_saved_state_t state;
    spin_lock_save(&print_spin_lock, &state, PRINT_LOCK_FLAGS);

    list_add_head(&print_callbacks, &cb->entry);

    spin_unlock_restore(&print_spin_lock, state, PRINT_LOCK_FLAGS);
}

void unregister_print_callback(print_callback_t *cb)
{
    spin_lock_saved_state_t state;
    spin_lock_save(&print_spin_lock, &state, PRINT_LOCK_FLAGS);

    list_delete(&cb->entry);

    spin_unlock_restore(&print_spin_lock, state, PRINT_LOCK_FLAGS);
}

static int __debug_stdio_write(void *ctx, const char *s, size_t len)
{
    out_count(s, len);
    return len;
}

static int __debug_stdio_fgetc(void *ctx)
{
    char c;
    int err;

    err = platform_dgetc(&c, true);
    if (err < 0)
        return err;
    return (unsigned char)c;
}

#define DEFINE_STDIO_DESC(id)                       \
    [(id)]  = {                         \
        .ctx        = &__stdio_FILEs[(id)],         \
        .write      = __debug_stdio_write,          \
        .fgetc      = __debug_stdio_fgetc,          \
    }

FILE __stdio_FILEs[3] = {
    DEFINE_STDIO_DESC(0), /* stdin */
    DEFINE_STDIO_DESC(1), /* stdout */
    DEFINE_STDIO_DESC(2), /* stderr */
};
#undef DEFINE_STDIO_DESC

