/*  $Id$

    Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        wielemak@science.uva.nl
    WWW:           http://www.swi-prolog.org
    Copyright (C): 1985-2007, University of Amsterdam

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef PL_SEGSTACK_H_INCLUDED
#define PL_SEGSTACK_H_INCLUDED

#define SEGSTACK_CHUNKSIZE (1*1024)

typedef struct segchunk
{ struct segchunk *next;		/* double linked list */
  struct segchunk *previous;
  char  *top;				/* top when closed */
  int	 allocated;			/* must call free on it */
  size_t size;				/* size of the chunk */
  char	 data[1];			/* data on my back */
} segchunk;

typedef struct
{ segchunk *first;
  segchunk *last;
  size_t   unit_size;
  char	   *base;
  char	   *top;
  char	   *max;
  size_t    count;
} segstack;


#define popSegStack(stack, to, type) \
	( ((stack)->top >= (stack)->base + sizeof(type))	\
		? ( (stack)->top -= sizeof(type),		\
		    *to = *(type*)(stack)->top,			\
		    (stack)->count--,				\
		    TRUE					\
		  )						\
		: popSegStack_((stack), to)			\
	)

#define pushSegStack(stack, data, type) \
	( ((stack)->top + (stack)->unit_size <= (stack)->max)	\
		? ( *(type*)(stack)->top = data,			\
		    (stack)->top += sizeof(type),		\
		    (stack)->count++,				\
		    TRUE					\
		  )						\
		: pushSegStack_((stack), &data)			\
	)

COMMON(void)	initSegStack(segstack *stack, size_t unit_size,
			     size_t len, void *data);
COMMON(int)	pushSegStack_(segstack *stack, void* data) WUNUSED;
COMMON(int)	pushRecordSegStack(segstack *stack, Record r) WUNUSED;
COMMON(int)	popSegStack_(segstack *stack, void *data);
COMMON(void*)	topOfSegStack(segstack *stack);
COMMON(void)	popTopOfSegStack(segstack *stack);
COMMON(void)	scanSegStack(segstack *s, void (*func)(void *cell));
COMMON(void)	clearSegStack(segstack *s);

		 /*******************************
		 *	       INLINE		*
		 *******************************/

static inline void
topsOfSegStack(segstack *stack, int count, void **tops)
{ char *p = stack->top - stack->unit_size;
  char *base = stack->base;

#ifdef O_DEBUG
  assert(stack->count >= count);
#endif

  for(;;)
  { while(count > 0 && p >= base)
    { *tops++ = p;
      p -= stack->unit_size;
      count--;
    }

    if ( count > 0 )
    { segchunk *chunk = stack->last->previous;

      p = chunk->top - stack->unit_size;
      base = chunk->data;
    } else
      break;
  }
}


#endif /*PL_SEGSTACK_H_INCLUDED*/
