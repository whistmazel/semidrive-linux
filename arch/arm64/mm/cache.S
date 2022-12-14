/*
 * Cache maintenance
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>
#include <asm/asm-uaccess.h>

#if defined(CONFIG_GK20A_PCI)
/*
 *	v8_op_dcache_all op
 *
 * op=cisw, Flush the whole D-cache.
 * op=csw, Clean the whole D-cache.
 *
 *	Corrupted registers: x0-x7, x9-x11
 */
.macro v8_op_dcache_all op			// op=csw clean, op=cisw flush
	cbz	x3, 5f			// if loc is 0, then no need to clean
	mov	x10, #0				// start clean at cache level 0
1:
	add	x2, x10, x10, lsr #1		// work out 3x current cache level
	lsr	x1, x0, x2			// extract cache type bits from clidr
	and	x1, x1, #7			// mask of the bits for current cache only
	cmp	x1, #2				// see what cache we have at this level
	b.lt	4f				// skip if no cache, or just i-cache
	save_and_disable_irq x9			// make CSSELR and CCSIDR access atomic
	msr	csselr_el1, x10			// select current cache level in csselr
	isb					// isb to sych the new cssr&csidr
	mrs	x1, ccsidr_el1			// read the new ccsidr
	restore_irq x9
	and	x2, x1, #7			// extract the length of the cache lines
	add	x2, x2, #4			// add 4 (line length offset)
	mov	x4, #0x3ff
	and	x4, x4, x1, lsr #3		// find maximum number on the way size
	clz	w5, w4				// find bit position of way size increment
	mov	x7, #0x7fff
	and	x7, x7, x1, lsr #13		// extract max number of the index size
2:
	mov	x9, x4				// create working copy of max way size
3:
	lsl	x6, x9, x5
	orr	x11, x10, x6			// factor way and cache number into x11
	lsl	x6, x7, x2
	orr	x11, x11, x6			// factor index number into x11
	dc	\op, x11			// op=csw/cisw, clean/flush by set/way
	subs	x9, x9, #1			// decrement the way
	b.ge	3b
	subs	x7, x7, #1			// decrement the index
	b.ge	2b
4:
	add	x10, x10, #2			// increment cache number
	cmp	x3, x10
	b.gt	1b
5:
	mov	x10, #0				// swith back to cache level 0
	msr	csselr_el1, x10			// select current cache level in csselr
	dsb	sy
	isb
	ret
.endm

/*
 *	__flush_dcache_all()
 *
 *	Flush the whole D-cache.
 *
 *	Corrupted registers: x0-x7, x9-x11
 */
ENTRY(__flush_dcache_all)
	.weak	__flush_dcache_all
	dmb	sy				// ensure ordering with previous memory accesses
	mrs	x0, clidr_el1			// read clidr
	and	x3, x0, #0x7000000		// extract loc from clidr
	lsr	x3, x3, #23			// left align loc bit field

	v8_op_dcache_all cisw
ENDPROC(__flush_dcache_all)

/*
 *	__clean_dcache_all()
 *
 *	Clean the whole D-cache.
 *
 *	Corrupted registers: x0-x7, x9-x11
 */
ENTRY(__clean_dcache_all)
	.weak	__clean_dcache_all
	dmb	sy				// ensure ordering with previous memory accesses
	mrs	x0, clidr_el1			// read clidr
	and	x3, x0, #0x7000000		// extract loc from clidr
	lsr	x3, x3, #23			// left align loc bit field

	v8_op_dcache_all csw
ENDPROC(__clean_dcache_all)

/*
 *	__flush_dcache_louis()
 *
 *	Flush D-cache to the level of unification inner shareable
 *
 *	Corrupted registers: x0-x7, x9-x11
 */
ENTRY(__flush_dcache_louis)
	dmb	sy				// ensure ordering with previous memory accesses
	mrs	x0, clidr_el1			// read clidr
	and	x3, x0, #0xe00000		// extract louis from clidr
	lsr	x3, x3, #20			// left align louis bit field
	v8_op_dcache_all cisw
ENDPROC(__flush_dcache_louis)

/*
 *	__clean_dcache_louis()
 *
 *	Clean D-cache to the level of unification inner shareable
 *
 *	Corrupted registers: x0-x7, x9-x11
 */
ENTRY(__clean_dcache_louis)
	dsb	sy				// ensure ordering with previous memory accesses
	mrs	x0, clidr_el1			// read clidr
	and	x3, x0, #0xe00000		// extract louis from clidr
	lsr	x3, x3, #20			// left align louis bit field
	v8_op_dcache_all csw
ENDPROC(__clean_dcache_louis)

/*
 *	flush_cache_all()
 *
 *	Flush the entire cache system.  The data cache flush is now achieved
 *	using atomic clean / invalidates working outwards from L1 cache. This
 *	is done using Set/Way based cache maintainance instructions.  The
 *	instruction cache can still be invalidated back to the point of
 *	unification in a single instruction.
 */
ENTRY(flush_cache_all)
	.weak	flush_cache_all
	mov	x12, lr
	bl	__flush_dcache_all
	mov	x0, #0
	ic	ialluis				// I+BTB cache invalidate
	ret	x12
ENDPROC(flush_cache_all)

/*
 *	flush_cache_louis()
 *
 *	Actually a clean should be sufficient for PG entry
 */
ENTRY(flush_dcache_louis)
	mov	x12, lr
	bl	__flush_dcache_louis
	mov	x0, #0
	ret	x12
ENDPROC(flush_dcache_louis)
#endif

/*
 *	flush_icache_range(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(flush_icache_range)
	/* FALLTHROUGH */

/*
 *	__flush_cache_user_range(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
ENTRY(__flush_cache_user_range)
	uaccess_ttbr0_enable x2, x3, x4
	dcache_line_size x2, x3
	sub	x3, x2, #1
	bic	x4, x0, x3
1:
user_alt 9f, "dc cvau, x4",  "dc civac, x4",  ARM64_WORKAROUND_CLEAN_CACHE
	add	x4, x4, x2
	cmp	x4, x1
	b.lo	1b
	dsb	ish

	icache_line_size x2, x3
	sub	x3, x2, #1
	bic	x4, x0, x3
1:
USER(9f, ic	ivau, x4	)		// invalidate I line PoU
	add	x4, x4, x2
	cmp	x4, x1
	b.lo	1b
	dsb	ish
	isb
	mov	x0, #0
1:
	uaccess_ttbr0_disable x1, x2
	ret
9:
	mov	x0, #-EFAULT
	b	1b
ENDPROC(flush_icache_range)
ENDPROC(__flush_cache_user_range)

/*
 *	__flush_dcache_area(kaddr, size)
 *
 *	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 *	are cleaned and invalidated to the PoC.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__flush_dcache_area)
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
ENDPIPROC(__flush_dcache_area)

/*
 *	__clean_dcache_area_pou(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoU.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__clean_dcache_area_pou)
	dcache_by_line_op cvau, ish, x0, x1, x2, x3
	ret
ENDPROC(__clean_dcache_area_pou)

/*
 *	__inval_dcache_area(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are invalidated. Any partial lines at the ends of the interval are
 *	also cleaned to PoC to prevent data loss.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__inval_dcache_area)
	/* FALLTHROUGH */

/*
 *	__dma_inv_area(start, size)
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
__dma_inv_area:
	add	x1, x1, x0
	dcache_line_size x2, x3
	sub	x3, x2, #1
	tst	x1, x3				// end cache line aligned?
	bic	x1, x1, x3
	b.eq	1f
	dc	civac, x1			// clean & invalidate D / U line
1:	tst	x0, x3				// start cache line aligned?
	bic	x0, x0, x3
	b.eq	2f
	dc	civac, x0			// clean & invalidate D / U line
	b	3f
2:	dc	ivac, x0			// invalidate D / U line
3:	add	x0, x0, x2
	cmp	x0, x1
	b.lo	2b
	dsb	sy
	ret
ENDPIPROC(__inval_dcache_area)
ENDPROC(__dma_inv_area)

/*
 *	__clean_dcache_area_poc(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoC.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__clean_dcache_area_poc)
	/* FALLTHROUGH */

/*
 *	__dma_clean_area(start, size)
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
__dma_clean_area:
	dcache_by_line_op cvac, sy, x0, x1, x2, x3
	ret
ENDPIPROC(__clean_dcache_area_poc)
ENDPROC(__dma_clean_area)

/*
 *	__clean_dcache_area_pop(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoP.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
ENTRY(__clean_dcache_area_pop)
	dcache_by_line_op cvap, sy, x0, x1, x2, x3
	ret
ENDPIPROC(__clean_dcache_area_pop)

/*
 *	__dma_flush_area(start, size)
 *
 *	clean & invalidate D / U line
 *
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
ENTRY(__dma_flush_area)
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
ENDPIPROC(__dma_flush_area)

/*
 *	__dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(__dma_map_area)
	cmp	w2, #DMA_FROM_DEVICE
	b.eq	__dma_inv_area
	b	__dma_clean_area
ENDPIPROC(__dma_map_area)

/*
 *	__dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
ENTRY(__dma_unmap_area)
	cmp	w2, #DMA_TO_DEVICE
	b.ne	__dma_inv_area
	ret
ENDPIPROC(__dma_unmap_area)

ENTRY(arch_flush_dcache_all)
    /* Clean and Invalidate dCache */
    mrs x0, clidr_el1
    and w3, w0, #0x07000000     // Bits: 26:24 Level of Coherence
    lsr w3, w3, #23             // Store 2 x LoC in W3
    cbz w3, Finished            // If 0, we are done
    mov w10, #0                 // store 2x cache level (since csselr starts at bit 1)
    mov w8, #1

Loop1:
    add w2, w10, w10, lsr #1    // Calculate 3x cache level a(w10 + 2w10 = 3w10)
    lsr w1, w0, w2              // read cType (cache type)
    and w1, w1, #0x7            // mask 3-bits
    cmp w1, #2                  // types >=2 include data cache
    b.lt Skip                   // skip if no data cache implemented
    msr csselr_el1, x10         // select the cache level
    isb                         // sync
    mrs x1, ccsidr_el1          // read ccsidr (current cache size id)
    and w2, w1, #0x7            // w2 = log2(linesize) - 4
    add w2, w2, #4              // w2 = log2(linesize)
    ubfx w4, w1, #3, #10        // w4 = way number (associativity)
    clz w5, w4                  // w5 = 32 - log2(ways), bit pos in dc operand
    lsl w9, w4, w5              // w9 = max way number, aligned to position in dc operand
    lsl w16, w8, w5             // w16 = amount to decrement way number per iteration

Loop2:
    ubfx w7, w1, #13, #15       // w7 = max set number
    lsl w7, w7, w2              // w7= max set number, aligned to position in dc operand
    lsl w17, w8, w2             // w17 = amount to decrement set number per iteration

Loop3:
    orr w11, w10, w9            // w11 = combine way number, cache number and set num for dc operand
    orr w11, w11, w7
    dc cisw, x11                 // perform clean by set and way
    subs w7, w7, w17            // decrement set number
    b.ge Loop3
    subs x9, x9, x16            // decrement way number
    b.ge Loop2

Skip:
    add w10, w10, #2
    cmp w3, w10
    dsb sy
    b.gt Loop1

Finished:
    ret
ENDPIPROC(arch_flush_dcache_all)
