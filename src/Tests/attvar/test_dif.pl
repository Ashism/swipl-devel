/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (C): 2008-2015, University of Amsterdam
			      VU University Amsterdam

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

:- module(test_dif,
	  [ test_dif/0
	  ]).

dif(1) :-
	dif(1, A), \+ A = 1.
dif(2) :-
	dif(1, A), dif(2, A), \+ A = 1.
dif(3) :-
	dif(1, A), dif(2, A), \+ A = 2.
dif(4) :-
	dif(A, B), A = 1, \+ B = 1.
dif(5) :-
	A = a(A, 1),
	B = a(B, X),
	dif(A, B), \+ X = 1.
dif(6) :-
	dif(a(x(1,2), B), a(X, 1)),
	X = a,
	\+ attvar(B).
dif(7) :-
	dif(a(x(1,2), B), a(X, 1)),
	X = x(1,2),
	\+ B = 1.
dif(8) :-
	dif(a(x(1,2), B), a(X, 1)),
	X = x(1,Y),
	Y = 3,
	\+ attvar(B).
dif(9) :-
	dif(X, Y), \+ X = Y.
dif(10) :-
	dif(f(X,_Z),f(a,b)),
	dif(f(X,Y),f(b,b)),
	X = a, Y = b.
dif(11) :-
	dif(A,B), memberchk(A, [B, C]),
	A == C.
dif(12) :-		% https://github.com/SWI-Prolog/issues/issues/15
	dif(X-Y,1-2), X=Y, Y = 1.
dif(13) :-		% https://github.com/SWI-Prolog/issues/issues/15
	dif(X-Y,1-2), X=Y, Y = 2.

:- dynamic
	failed/1.

test_dif :-
	retractall(failed(_)),
	forall(clause(dif(N), _, _),
	       (   dif(N)
	       ->  true
	       ;   format('~NFailed: ~w~n', [dif(N)]),
		   assert(failed(N))
	       )),
	\+ failed(_).

