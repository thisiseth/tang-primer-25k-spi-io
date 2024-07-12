
// support routines for the main code
// - output to serial port
// - timer
// - cut-down printf routine
// - memset
// - malloc & free

typedef unsigned int   uint32_t;
typedef unsigned char  uint8_t;
typedef uint32_t       time_t;

#define NULL    ((void*)0)

uint32_t *uart = (uint32_t *)0x20000000;

void putc(char c)
{
	while((uart[1] & 0x01) == 0);
	*uart = c;
}

void puts(char *s)
{
	while(*s) putc(*s++);
}

int is_sim(void)
{
	return uart[1] & 0x4;
}

void wait_ms(time_t ms)
{
    if (is_sim()) return;

	time_t end = uart[2] + ms;
	while (uart[2] != end) ;
}

time_t now_ms(void)
{
	return uart[2];
}


// We run on RV32I, so no hw '/' and '%' available
//
static uint32_t rem;
uint32_t udiv(uint32_t n, uint32_t m)
{
    uint32_t bit = 1, result = 0;
    while (m <= n && ((int)m) > 0) {
        m   <<= 1;
        bit <<= 1;
    }
    if (m <= n && ((int)m) < 0 ) {
        n -= m;
        result += bit;
    }
    while (bit > 1) {
        m   >>= 1;
        bit >>= 1;
        if (n >= m) {
            n -= m;
            result += bit;
        }
    }
    rem = n;
    return result;
}

// Scaled down version of C Library printf.
// Only %c, %s %u %d (==%u) %o %x are recognized.
// Used to print diagnostic information
//
void printn(uint32_t n, uint32_t b)
{
	int i, plmax;
	char d[12];

    plmax = (b==8) ? 11 : (b==10) ? 10 : 8;
	for (i=0; i < plmax; i++) {
		n = udiv(n, b);
		d[i] = rem;
		if (n==0) break;
	}
	if (i == plmax) i--;
	for ( ; i >= 0; i--) {
		putc("0123456789abcdef"[d[i]]);
	}
}

void printf(char *fmt, unsigned int x1)
{
	int c;
	unsigned int *adx;
	char *s;

	adx = &x1;
loop:
	while((c = *fmt++) != '%') {
		if(c == '\0')
			return;
		putc(c);
	}
	c = *fmt++;
	if(c == 'd' || c == 'u' || c == 'o' || c == 'x') {
		printn((long)*adx, c=='o'? 8: (c=='x'? 16:10));
	}
	else if(c == 'c') {
		putc(*adx);
	}
	else if(c == 's') {
		s = *(char **)adx;
		adx++;
		while(c = *s++)
			putc(c);
	}
	adx++;
	goto loop;
}

void* memset(void *dest, uint8_t val, uint32_t len)
{
    uint8_t *ptr = dest;

    while (len-- > 0)
        *ptr++ = val;
    return dest;
}

// K&R malloc and free. malloc() also clears the memory block.
//
struct memhdr {
  struct memhdr *next;  // next block if on free list
  uint32_t       size;  // size of this block
};
typedef struct memhdr HDR;

// pool size in 8 byte clicks: 32 = 256 bytes of malloc pool
#define MALLOCSZ    32

static HDR  base;            // zero sized list anchor, requirement: &base < &core
static HDR  core[MALLOCSZ];  // allocation pool in 8 byte units
static HDR* freep;           // start of free list

void* malloc(uint32_t nbytes)
{
    HDR *p, *prevp;
    uint32_t nunits = ((nbytes+7) >> 3) + 1;

    if((prevp = freep) == NULL) {
        core->next = prevp = freep = &base;
        core->size = MALLOCSZ;
        base.next  = &core[0];
        base.size  = 0;
    }
    
    for(p = prevp->next; ; prevp = p, p = p->next) {
        if(p->size>=nunits) {
            if(p->size==nunits)
                // exact fit
                prevp->next = p->next;
            else {
                // allocate tail end
                p->size -= nunits;
                p += p->size;
                p->size = nunits;
            }
            freep = prevp;
            memset(p+1, 0, nbytes);
            return (void*)(p+1);
        }
        if(p == freep) {
            printf("panic: NULL malloc\n", 0);
            return NULL;
        }
    }
}

void free(void* ap)
{
    HDR *p, *bp = (HDR*)ap - 1;
    
    for(p = freep; !(bp > p && bp < p->next); p = p->next) {
        if (p >= p->next && (bp > p || bp < p->next))
            break; // freed block at start or end of arena
    }
    
    if(bp + bp->size == p->next) { // join to upper
        bp->size += p->next->size;
        bp->next  = p->next->next;
    }
    else
        bp->next = p->next;
        
    if(p + p->size == bp) { // join to lower
        p->size += bp->size;
        p->next  = bp->next;
    }
    else
        p->next = bp;

    freep = p;
}
