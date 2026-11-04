/*
 * A bounded _sbrk for newlib.
 *
 * The toolchain links with --specs=nosys.specs, whose _sbrk grows the heap
 * from _ebss with no limit: nothing stops it from growing into the stack.
 * newlib calls it behind the scenes even in code that never calls malloc
 * itself -- for example, nano-newlib's float printf conversion mallocs a
 * scratch buffer (with -u _printf_float linked in; without it, %f and %g
 * print nothing), so a firmware's first printf("%f") already grows a heap
 * toward the stack.
 *
 * This BSP's _sbrk instead refuses to grow the heap past _heap_limit, a
 * symbol the linker script defines as _estack - _min_stack_size (the start
 * of the fixed stack reservation), and returns -1 with errno set to ENOMEM
 * once the heap would reach it. It replaces the one in libnosys.a (see
 * the -u _sbrk link option in cmake/e16.cmake).
 *
 * It's weak, so a firmware that manages its own RAM layout (e.g. a stack
 * canary or a tighter heap limit) can define its own _sbrk instead.
 */
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

extern uint8_t _ebss;
extern uint8_t _heap_limit;

static uint8_t *heap_end;

__attribute__((weak)) void *_sbrk(ptrdiff_t incr)
{
    if (!heap_end) {
        heap_end = &_ebss;
    }

    uint8_t *prev = heap_end;
    if (incr > 0 && (uintptr_t)(&_heap_limit - prev) < (uintptr_t)incr) {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_end = prev + incr;
    return prev;
}
