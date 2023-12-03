#ifndef RING_H_
#define RING_H_
#include <stdint.h>
#include <immintrin.h>

/* true if x is a power of 2 */
#define POWEROF2(x) ((((x)-1) & (x)) == 0)

#define RING_F_SP_ENQ 0x0001 /**< The default enqueue is "single-producer". */
#define RING_F_SC_DEQ 0x0002 /**< The default dequeue is "single-consumer". */

#define RING_F_EXACT_SZ 0x0004
#define RTE_RING_SZ_MASK  (0x7fffffffU) /**< Ring size mask */

#define RING_F_MP_RTS_ENQ 0x0008 /**< The default enqueue is "MP RTS". */
#define RING_F_MC_RTS_DEQ 0x0010 /**< The default dequeue is "MC RTS". */


/* mask of all valid flag values to ring_create() */
#define RING_F_MASK (RING_F_SP_ENQ | RING_F_SC_DEQ | RING_F_EXACT_SZ | \
		     RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ )

#define RTE_CACHE_LINE_SIZE 64

#define RTE_ALIGN_FLOOR(val, align) \
	(typeof(val))((val) & (~((typeof(val))((align) - 1))))

#define RTE_ALIGN_CEIL(val, align) \
	RTE_ALIGN_FLOOR(((val) + ((typeof(val)) (align) - 1)), align)

#define RTE_ALIGN(val, align) RTE_ALIGN_CEIL(val, align)


#define unlikely(x)  (x)
#define likely(x)  (x)

/** enqueue/dequeue behavior types */
enum rte_ring_queue_behavior {
	/** Enq/Deq a fixed number of items from a ring */
	RTE_RING_QUEUE_FIXED = 0,
	/** Enq/Deq as many items as possible from ring */
	RTE_RING_QUEUE_VARIABLE
};


/** prod/cons sync types */
enum rte_ring_sync_type {
	RTE_RING_SYNC_MT,     /**< multi-thread safe (default mode) */
	RTE_RING_SYNC_ST,     /**< single thread only */
};

struct rte_ring_headtail {
	volatile uint32_t head;      /**< prod/consumer head. */
	volatile uint32_t tail;      /**< prod/consumer tail. */
	enum rte_ring_sync_type sync_type;
};

struct rte_ring {
	int flags;               /**< Flags supplied at creation. */
	uint32_t size;           /**< Size of ring. */
	uint32_t mask;           /**< Mask (size-1) of ring. */
	uint32_t capacity;       /**< Usable size of ring */

	/** Ring producer status. */
	struct rte_ring_headtail prod;

	/** Ring consumer status. */
	struct rte_ring_headtail cons;
};

#define MPLOCKED        "lock ; "       /**< Insert MP lock prefix. */

static inline int
rte_atomic32_cmpset(volatile uint32_t *dst, uint32_t exp, uint32_t src)
{
	uint8_t res;

	asm volatile(
			MPLOCKED
			"cmpxchgl %[src], %[dst];"
			"sete %[res];"
			: [res] "=a" (res),     /* output */
			  [dst] "=m" (*dst)
			: [src] "r" (src),      /* input */
			  "a" (exp),
			  "m" (*dst)
			: "memory");            /* no-clobber list */
	return res;
}

static inline uint32_t rte_combine32ms1b(uint32_t x)
{
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;

	return x;
}

static inline uint32_t rte_align32pow2(uint32_t x)
{
	x--;
	x = rte_combine32ms1b(x);

	return x + 1;
}

static inline void rte_pause(void)
{
	_mm_pause();
}

int rte_ring_enqueue(struct rte_ring *r, void *obj);
int rte_ring_dequeue(struct rte_ring *r, void **obj_p);
struct rte_ring *rte_ring_create(unsigned int count, unsigned int flags);
void rte_ring_free(struct rte_ring *r);

#endif
