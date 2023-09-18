#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <malloc/malloc.h>
#include <os/lock.h>

#include <pthread.h>

static malloc_zone_t system_malloc_zone;

static void my_print_err(const char *s) {
    write(1, s, strlen(s));
}

struct my_thread { // align to cache-line
    bool init;
    os_unfair_lock spin;
} __attribute__((aligned(64)));
static struct my_thread my_threads[0xffff];

static struct my_thread *my_lock() {
    int id = pthread_mach_thread_np(pthread_self());

    if (id > 0xffff) {
        char text[200];
        sprintf(text, "thread id too large : %d\n", id);
//        my_print_err("thread id too large.\n");
        my_print_err(text);
        abort();
    }

    struct my_thread *my = &my_threads[id % 0xffff];

    if (my->init == false) {
        my_print_err("init\n");
        my->spin = OS_UNFAIR_LOCK_INIT;
        my->init = true;
    }

    bool xtry = os_unfair_lock_trylock(&my->spin);
    if (xtry == false) {
        // Here a reentrant *alloc() is ongoing.
        // If it is confirmed and rare (bad luck on libunwind malloc() during a malloc(),
        //  you could possibly handle and return some allocations from a mmap() ?
        my_print_err("reentrant lock\n");
        os_unfair_lock_lock(&my->spin);
    }

    return my;
}

void my_unlock(struct my_thread *my) {
//    if (xtry == true) {
        os_unfair_lock_unlock(&my->spin);
//    }
}

static void* my_malloc(struct _malloc_zone_t *zone, size_t size) {
//    my_print_err("my_malloc... begin...\n");

    struct my_thread *my = my_lock();

    void* ret = system_malloc_zone.malloc(zone, size);

    my_unlock(my);

    if (ret == NULL) {
        my_print_err("malloc failed, aborting\n");
        abort();
    }

//    my_print_err("my_malloc... end...\n");

    return ret;
}

static void *my_calloc(struct _malloc_zone_t *zone, size_t num_items, size_t size) {
//    my_print_err("my_calloc...\n");
    struct my_thread *my = my_lock();
    void *ret = system_malloc_zone.calloc(zone, num_items, size);
    my_unlock(my);
    return ret;
}

static void *my_valloc(struct _malloc_zone_t *zone, size_t size) {
    my_print_err("my_valloc...\n");
    struct my_thread *my = my_lock();
    void *ret = system_malloc_zone.valloc(zone, size);
    my_unlock(my);
    return ret;
}

static void *my_realloc(struct _malloc_zone_t *zone, void *ptr, size_t size) {
//    my_print_err("my_realloc...\n");
    struct my_thread *my = my_lock();
    void *ret = system_malloc_zone.realloc(zone, ptr, size);
    my_unlock(my);
    return ret;
}

static void *my_memalign(struct _malloc_zone_t *zone, size_t alignment, size_t size) {
    struct my_thread *my = my_lock();
    void *ret = system_malloc_zone.memalign(zone, alignment, size);
    my_unlock(my);
    return ret;
}
static void my_free_definite_size(struct _malloc_zone_t *zone, void *ptr, size_t size) {
    struct my_thread *my = my_lock();
    system_malloc_zone.free_definite_size(zone, ptr, size);
    my_unlock(my);
}
static size_t my_pressure_relief(struct _malloc_zone_t *zone, size_t goal) {
    struct my_thread *my = my_lock();
    size_t ret = system_malloc_zone.pressure_relief(zone, goal);
    my_unlock(my);
    return ret;
}
static boolean_t my_claimed_address(struct _malloc_zone_t *zone, void *ptr) {
    struct my_thread *my = my_lock();
    boolean_t ret = system_malloc_zone.claimed_address(zone, ptr);
    my_unlock(my);
    return ret;
}
static void my_try_free_default(struct _malloc_zone_t *zone, void *ptr) {
    struct my_thread *my = my_lock();
    system_malloc_zone.try_free_default(zone, ptr);
    my_unlock(my);
}

static void zones_replace_mine(malloc_zone_t *zone) {
    system_malloc_zone = *zone;

    if (mprotect(zone, getpagesize(), PROT_READ | PROT_WRITE) != 0) {
        my_print_err("munprotect failed\n");
        abort();
    }

//    zone->malloc = system_malloc_zone.malloc;
    zone->malloc = my_malloc;
    zone->calloc = my_calloc;
    zone->valloc = my_valloc;
    zone->realloc = my_realloc;

    zone->memalign = my_memalign;
    zone->free_definite_size = my_free_definite_size;
    zone->pressure_relief = my_pressure_relief;
    zone->claimed_address = my_claimed_address;
    zone->try_free_default = my_try_free_default;

    if (mprotect(zone, getpagesize(), PROT_READ) != 0) {
        my_print_err("mprotect failed\n");
        abort();
    }

}

void cgo_init() {
    malloc_zone_t *zone = malloc_default_zone();
    if (zone->version != 8 && zone->version != 10) {
        my_print_err("Unknown malloc zone version\n");

        char text[200];
        sprintf(text, "Unknown malloc zone version: %d\n", zone->version);
        my_print_err(text);

        abort();
    }

    printf("zone %p name: %s malloc %p\n", zone, zone->zone_name, zone->malloc);
//    system_malloc_zone = *zone;

//    zones_replace_mine(zone);

    {
        malloc_zone_t **zones = NULL;
    	unsigned int num_zones = 0;

        malloc_get_all_zones(0, NULL, (vm_address_t**)&zones, &num_zones);
        printf("zones nb: %d\n", num_zones);
        for (int i = 0; i < num_zones; i++) {
            malloc_zone_t *z = zones[i];

            printf("zone[%d] : %p name %s malloc %p\n", i, z, z->zone_name, z->malloc);
            if (i == 0)
                zones_replace_mine(z);
        }
    }


    my_print_err("test begin\n");
    printf("malloc 10k: %p\n", malloc(10000));
    printf("malloc large: %p\n", malloc(1000000000000l));
    my_print_err("test end\n");
}

#ifdef MAIN
int main() {
    cgo_init();

//    printf("sizeof mach_port_t : %ld\n", sizeof(mach_port_t));
//    for (int i =0; i < 1000000000; i++)
//    {
////        pthread_self();
//        int id = pthread_mach_thread_np(pthread_self());
//        if (i % 100000000 == 0)
//            printf("id: %d\n", id);
//    }
//    printf("sizeof my_threads: %ld\n", sizeof(my_threads));

    return 0;
}
#endif
