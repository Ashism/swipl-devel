/*  $Id$

    E-mail: jan@swi.psy.uva.nl

    Copyright (C) 1996 University of Amsterdam. All rights reserved.
*/

:- set_prolog_flag(optimise, true).
%:- set_prolog_flag(trace_gc, true).

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SWI-Prolog test file.  A test is a clause of the form:

	<TestSet>(<Name>-<Number>) :- Body.

If the body fails, an appropriate  error   message  is  printed. So, all
goals are supposed to  succeed.  The   predicate  testset/1  defines the
available test sets. The public goals are:

	?- runtest(+TestSet).
	?- test.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

:- format('SWI-Prolog test suite.  To run all tests run ?- test.~n~n', []).

% Required to get this always running regardless of user LANG setting.
% Without this the tests won't run on machines with -for example- LANG=ja
% according to NIDE Naoyuki, nide@ics.nara-wu.ac.jp.  Thanks!

:- getenv('LANG', _) -> setenv('LANG', 'C'); true.


		 /*******************************
		 *	      SYNTAX		*
		 *******************************/

syntax(op-1) :-
	atom_to_term("3+4*5", +(3,*(4,5)), []).
syntax(op-2) :-
	atom_to_term("1+2+3", +(+(1,2),3), []).
syntax(op-3) :-
	catch(atom_to_term("a:-b:-c", _, _), E, true),
	E = error(syntax_error(operator_clash), _).
syntax(op-4) :-
	op(600, fx, op1),
	atom_to_term("op1 1+2", op1(+(1,2)), []).
syntax(op-5) :-
	op(600, fx, op1),
	catch(atom_to_term("op1 op1 1", _, _), E, true),
	E = error(syntax_error(operator_clash), _).
syntax(op-6) :-
	op(600, fy, op1),
	atom_to_term("op1 op1 1", op1(op1(1)), []).
syntax(op-7) :-
	op(600, fy, op1),
	op(500, xf, op2),
	catch(atom_to_term("op1 a op2", op1(op2(a)), []), E, true),
	E = error(syntax_error(operator_clash), _).
syntax(atom-1) :-
	atom_codes('\003\\'\n\x80\', X),
	X = [3, 39, 10, 128].
syntax(char-1) :-
	10 = 0'\n.
syntax(char-2) :-
	52 = 0'\x34.
syntax(char-2) :-
	"\\" =:= 0'\\.
syntax(string-1) :-
	'\c ' = ''.
syntax(string-2) :-
	'x\c y' = xy.
syntax(number-1) :-			% check integer overflow translation
	Chars = "41234567891",
	name(X, Chars),
	sformat(S, '~0f', [X]),
	string_to_list(S, Chars).
syntax(number-2) :-
	catch(atom_to_term('2\'', _, _), E, true),
	E = error(syntax_error(illegal_number), _).


		 /*******************************
		 *	       UNIFY		*
		 *******************************/

%	Some cyclic unification tests (normal unification should be fixed
%	already).

unify(cycle-1) :-			% Kuniaki Mukai
	X = f(Y), Y=f(X), X=Y.
unify(cycle-2) :-			% Kuniaki Mukai
	X = f(X), Y=f(Y), X=f(Y).


		 /*******************************
		 *      INTEGER ARITHMETIC	*
		 *******************************/

arithmetic(between-1) :-
	between(0, 10, 5).
arithmetic(between-2) :-
	\+ between(0, 10, 20).
arithmetic(between-3) :-
	findall(X, between(1, 6, X), Xs),
	Xs == [1, 2, 3, 4, 5, 6].
arithmetic(between-4) :-
	findall(X, between(-4, -1, X), Xs),
	Xs == [-4, -3, -2, -1].
arithmetic(succ-1) :-
	succ(0, X), X == 1.
arithmetic(succ-2) :-
	\+ succ(_, 0).
arithmetic(succ-3) :-
	catch(succ(_, -1), E, true),
	E = error(domain_error(not_less_than_zero, -1), _).
arithmetic(plus-1) :-
	plus(1, 2, 3).

		 /*******************************
		 *	  SIMPLE THINGS		*
		 *******************************/

arithmetic(arith-1) :-
	A is 5 + 5,
	A == 10.
arithmetic(arith-2) :-
	0 =:= -5 + 2.5 * 2.
arithmetic(arith-3) :-
	A is pi,
	B is cos(A),
	B =:= -1.
arithmetic(arith-4) :-
	0 =:= 10 - 3.4 - 6.6.
arithmetic(arith-5) :-
	1 =:= integer(0.5).
arithmetic(arith-6) :-
	4.5 =:= abs(-4.5).
arithmetic(arith-7) :-
	5.5 =:= max(1, 5.5).
arithmetic(arith-8) :-
	-6 is min(-6, -5.5).
arithmetic(arith-9) :-
	4000 =:= integer(10000 * float_fractional_part(1e10 + 0.4)).
arithmetic(arith-10) :-
	-4000 =:= integer(10000 * float_fractional_part(-1e10 - 0.4)).
arithmetic(arith-11) :-
	current_prolog_flag(iso, ISO),
	set_prolog_flag(iso, true),
	1.0 is sin(pi/2),
	set_prolog_flag(iso, false),
	1   is sin(pi/2),
	set_prolog_flag(iso, ISO).
arithmetic(arith-12) :-
	1.0 is float(sin(pi/2)).
arithmetic(arith-13) :-
	1.0 =:= sin(pi/2).

		 /*******************************
		 *	    BIG NUMBERS		*
		 *******************************/

arithmetic(int-1) :-
	A is 1<<31, integer(A).
arithmetic(cmp-1) :-
	A is 100e6, 67 < A.


		 /*******************************
		 *	      FLOATS		*
		 *******************************/

foverflow(X) :-
	X2 is X * 1000,
	foverflow(X2),
	1 = 1.			% avoid tail-recursion to force termination

ftest(4.5).
ftest :-
	ftest(4.5).

floattest(float-1) :-
	ftest(X),
	X == 4.5.
floattest(float-2) :-
	ftest.
floattest(float-3) :-
	erase_all(f),
	recorda(f, 6.7),
	recorded(f, X),
	X == 6.7.
floattest(float-4) :-
	X is 10.67,
	X == 10.67.
floattest(float-5) :-
	clause(ftest(X), true),
	X == 4.5.
floattest(float-5) :-
	clause(ftest, ftest(X)),
	X == 4.5.
floattest(float-6) :-
	catch(foverflow(2), E, true),
	E = error(evaluation_error(float_overflow), _).
floattest(float-7) :-
	catch(foverflow(-2), E, true),
	E = error(evaluation_error(float_overflow), _).


		 /*******************************
		 *	 PROLOG FUNCTIONS	*
		 *******************************/

:- arithmetic_function(ten/0).
:- arithmetic_function(twice/1).
:- arithmetic_function(mean/2).
:- arithmetic_function(euler/0).

ten(10).
twice(X, R) :-
	R is X * 2.
mean(X1, X2, R) :-
	R is (X1 + X2)/2.

euler(2.71828).

arithmetic_functions(func-1) :-
	A is ten, A =:= 10.
arithmetic_functions(func-2) :-
	A is twice(5), A =:= 10.
arithmetic_functions(func-3) :-
	A is mean(0, 20), A =:= 10.
arithmetic_functions(func-4) :-
        Exp = 6*euler*7*1,		% test functions corrupting stack
        EE is Exp,
	EE =:= 6*euler*7*1.

		 /*******************************
		 *	     CHARACTERS		*
		 *******************************/

chars(chars-1) :-
	A is "a",
	A == 97.
chars(chars-2) :-
	A is [a],			% if "a" --> [a]
	A == 97.


		 /*******************************
		 *	   META CALLING		*
		 *******************************/

foo:hello(world).

meta(call-1) :-
	call(ten(X)),
	X == 10.
meta(call-2) :-
	\+ call(ten(20)).
meta(call-3) :-
	\+ call((between(0,3,X), !, X = 2)).
meta(call-4) :-
	length(X, 100000), call((is_list(X) -> true ; fail)).
meta(call-5) :-
	call((X=a;X=b)), X = b.
meta(call-5) :-
	call((foo:hello(X)->true)), X = world.
meta(call-6) :-
	call((X=a,x(X)=Y)), Y == x(a).
meta(call-7) :-
	string_to_list(S, "hello world"),
	call((string(S), true)).
meta(call-8) :-
	call((foo:true, true)).
meta(call-9) :-
	call((A=x, B=x, A==B)).		% avoid I_CALL_FVX for dynamic call
meta(call-10) :-
	A = (	member(_,[1,2,3]),
		flag(a, F, F+1),
		(   F >= 999999
		->  fail
		;   true
		)
	    ),
	flag(a, Old, 0),
	forall(A, true),
	flag(a, 3, Old).
meta(call-11) :-
	catch(call(1), E, true),
	E =@= error(type_error(callable, 1), _).
meta(apply-1) :-
	apply(=, [a,a]).
meta(apply-2) :-
	apply(=(a), [a]).
meta(apply-3) :-
	apply(a=a, []).


		 /*******************************
		 *	      CLEANUP		*
		 *******************************/

:- dynamic
	clean_rval/1.

cleanup_1.
cleanup_2(a).
cleanup_2(b).
cleanup_3 :-
	fail.

cleanup(clean-1) :-
	retractall(clean_rval(_)),
	call_cleanup(cleanup_1, R, assert(clean_rval(R))),
	retract(clean_rval(exit)).
cleanup(clean-2) :-
	retractall(clean_rval(_)),
	call_cleanup(cleanup_2(_), R, assert(clean_rval(R))), !,
	retract(clean_rval(!)).
cleanup(clean-3) :-
	retractall(clean_rval(_)),
	\+ call_cleanup(cleanup_3, R, assert(clean_rval(R))),
	retract(clean_rval(fail)).
cleanup(clean-4) :-
	catch(call_cleanup(throw(a), true), E, true),
	E == a.
cleanup(clean-5) :-
	catch(call_cleanup(throw(a), throw(b)), E, true),
	E == b.
cleanup(clean-6) :-
	catch(call_cleanup(true, throw(b)), E, true),
	E == b.
cleanup(clean-7) :-
	catch(call_cleanup(fail, throw(b)), E, true),
	E == b.
cleanup(clean-8) :-
	retractall(clean_rval(_)),
	call_cleanup(bagof(x, cleanup_1, _Xs), Reason,
		     assert(clean_rval(Reason))),
	retract(clean_rval(exit)).



		 /*******************************
		 *	    DEPTH-LIMIT		*
		 *******************************/

dl_det(1) :- !.
dl_det(N) :-
	NN is N - 1,
	dl_det(NN).

dl_ndet(1).
dl_ndet(N) :-
	NN is N - 1,
	dl_ndet(NN).

dl_fail(1) :- !, fail.
dl_fail(N) :-
	NN is N - 1,
	dl_fail(NN).

:- arithmetic_function(fac/1).

fac(1, 1) :- !.
fac(N, V) :-
	NN is N - 1,
	fac(NN, V0),
	V is N*V0.


depth_limit(depth-1) :-
	call_with_depth_limit(dl_det(1), 10, 1),
	deterministic(true).
depth_limit(depth-2) :-
	call_with_depth_limit(dl_det(10), 10, 10).
depth_limit(depth-3) :-
	call_with_depth_limit(dl_det(10), 9, depth_limit_exceeded).
depth_limit(ndet-1) :-
	findall(X, 
		call_with_depth_limit(dl_ndet(5), 10, X),
		L),
	L = [5, depth_limit_exceeded].
depth_limit(fail-1) :-
	\+ call_with_depth_limit(dl_fail(2), 10, _).
depth_limit(arith-1) :-
	call_with_depth_limit(_A is fac(10), 8, depth_limit_exceeded).


		 /*******************************
		 *	    TYPE TESTS		*
		 *******************************/

type_test(type-1) :-
	var(_), X = Y, var(X), Y = a, nonvar(X).
type_test(type-2) :-
	atom(hello), \+ atom(10), \+ atom("hello").


		 /*******************************
		 *	   TERM-HACKING		*
		 *******************************/

term(functor-1) :-
	functor(test(a, b), N, A), N == test, A == 2.
term(functor-2) :-
	functor(test(a, b), test, 2).
term(functor-3) :-
	functor(X, test, 2),
	forall(arg(_, X, A), var(A)).
term(arg-1) :-
	findall(N=A, arg(N, hello(a,b,c), A), T),
	T == [ 1=a, 2=b, 3=c ].
term(setarg-1) :-
	Term = foo(a, b),
	(   setarg(1, Term, c)
	->  Term == foo(c, b)
	).
term(setarg-2) :-
	Term = foo(a, b),
	(   setarg(1, Term, c),
	    garbage_collect,
	    fail
	;   Term == foo(a, b)
	).
term(univ-1) :-
	A =.. [a, B, B], A =@= a(C,C).
term(univ-2) :-
	A =.. [4.5], A == 4.5.
term(univ-3) :-
	3.4 =.. X, X == [3.4].
term(univ-4) :-
	a(a,b,c) =.. [a, a | L], L == [b,c].



		 /*******************************
		 *	       LIST		*
		 *******************************/

list(memberchk-1) :-
	memberchk(a, [b, a]).
list(memberchk-2) :-
	\+ memberchk(a, []).
list(memberchk-3) :-
	memberchk(a, L), memberchk(b, L), L =@= [a,b|_].
list(sort-1) :-
	sort([], []).
list(sort-2) :-
	sort([x], [x]).
list(sort-3) :-
	sort([e,b,c,e], [b,c,e]).
list(sort-4) :-
	msort([e,b,c,e], [b,c,e,e]).
list(sort-5) :-
	keysort([e-2,b-5,c-6,e-1], [b-5,c-6,e-2,e-1]).
list(sort-5) :-
	sort([a,g,b], [a,b|G]), G == [g].
list(sort-6) :-
	sort([X], [Y]), X == Y.
list(sort-7) :-
	sort([_X, _Y], [_,_]).


		 /*******************************
		 *	       SETS		*
		 *******************************/

foo(1, a).
foo(2, b).
foo(3, c).
foo(1, d).
foo(2, e).
foo(3, f).

type(atom).
type(S) :- string_to_atom(S, "string").
type(42).
type(3.14).
type([a, list]).
type(compound(1)).
type(compound(A, A)).
type(compound(_A, _B)).

set(X, 1) :- type(X).
set(X, 2) :- type(X).

sets(setof-1) :-
	setof(A-Pairs, setof(B, foo(A,B), Pairs), Result),
	Result = [1 - [a,d],2 - [b,e],3 - [c,f]].
sets(setof-2) :-
	setof(X-Ys, setof(Y, set(X,Y), Ys), R),
	string_to_atom(S, "string"),
	R =@= [3.14-[1, 2],
	       42-[1, 2],
	       atom-[1, 2],
	       S-[1, 2],
	       compound(1)-[1, 2],
	       [a, list]-[1, 2],
	       compound(_A, _B)-[1, 2],
	       compound(A, A)-[1, 2]].
sets(vars-1) :-
	'$e_free_variables'(A^satisfy(B^C^(setof(D:E,
						 (country(E), area(E, D)),
						 C),
					   aggregate(max, C, B),
					   in(B, A),
					   {place(A)})),
			    Free),
	Free == v(D, E).

		 /*******************************
		 *	       NAME		*
		 *******************************/

atom_handling(name-1) :-
	name(hello, X), X = "hello".
atom_handling(name-2) :-
	name(V, "5"), V == 5.
atom_handling(name-3) :-
	name(V, "5e4"), V =:= 50000.
atom_handling(name-4) :-
	name(V, "5e4a"), V == '5e4a'.
atom_handling(name-5) :-
	name(V, ""), V == ''.

atom_handling(atom-1) :-
	atom_length('hello', X), X == 5.
atom_handling(concat-1) :-
	atom_concat(gnu, gnat, gnugnat).
atom_handling(concat-2) :-
	atom_concat(X, gnat, gnugnat), X == gnu.
atom_handling(concat-3) :-
	atom_concat(gnu, X, gnugnat), X == gnat.
atom_handling(concat-4) :-
	atom_concat('', X, ''), X == ''.
atom_handling(concat-5) :-
	findall(X-Y, atom_concat(X, Y, 'abc'), Pairs),
	Pairs == [''-abc, a-bc, ab-c, abc-''].

atom_handling(number-1) :-
	atom_number('42', X), X == 42.
atom_handling(number-2) :-
	atom_number('1.0', X), float(X).
atom_handling(number-3) :-
	atom_number(X, 1.0), X == '1.0'.
atom_handling(number-4) :-
	atom_number(X, 42), X == '42'.

atom_handling(sub_atom-1) :-
	\+ sub_atom(a, _, _, 3, _).
atom_handling(sub_atom-1) :-
	\+ sub_atom(a, _, 3, _, _).
atom_handling(sub_atom-1) :-
	\+ sub_atom(a, 3, _, _, _).

atom_handling(current-1) :-
	findall(X, current_atom(X), Atoms),
	checklist(atom, Atoms),
	member(atom, Atoms),
	member(testset, Atoms),
	member('', Atoms),
	member(foobar, Atoms),
	length(Atoms, L),
	L > 100.			% else something is wrong!


		 /*******************************
		 *	      STRINGS		*
		 *******************************/

:- set_prolog_flag(backquoted_string, true).

string_handling(sub-1) :-
	\+ sub_string(`HTTP/1.1 404 Not Found`, _, _, _, `OK`).

:- set_prolog_flag(backquoted_string, false).


		 /*******************************
		 *	       DYNAMIC		*
		 *******************************/

cpxx.					% for test current_predicate-1
cpxx(_,_).

proc(retractall-1) :-
	forall(foo(A,B), assert(myfoo(A,B))),
	retractall(myfoo(2, _)),
	findall(A-B, myfoo(A,B), L1),
	L1 == [1-a, 3-c, 1-d, 3-f],
	retractall(myfoo(_,_)),
	findall(A-B, myfoo(A,B), L2),
	L2 == [].
proc(retract-1) :-
	forall(foo(A,B), assert(myfoo(A,B))),
	findall(X, retract(myfoo(1, X)), Xs),
	Xs == [a, d],
	forall(retract(myfoo(_,_)), true),
	\+ clause(myfoo(_,_), _).
proc(retract-2) :-
	assert((test(X, Y) :- X is Y + 3)),
	retract((test(A, B) :- Body)),
	Body == (A is B + 3).
proc(current_predicate-1) :-
	setof(X, current_predicate(cpxx/X), L), % order is not defined!
	L == [0, 2].

		 /*******************************
		 *	       CLAUSE		*
		 *******************************/

:- dynamic
	tcl/1.

tcl(a).
tcl(b) :- true.
tcl(c) :- write(hello).
tcl(a(X)) :- b(X).

mtcl:tcl(a) :- a.
mtcl:tcl(b) :- a, b.
mtcl:(tcl(c) :- a, b).

cl(clause-1) :-
	clause(tcl(a), X), X == true.
cl(clause-2) :-
	clause(tcl(b), X), X == true.
cl(clause-3) :-
	clause(tcl(c), X), X == write(hello).
cl(clause-4) :-
	clause(tcl(a(X)), B), B == b(X).
cl(clause-5) :-
	clause(tcl(H), b(a)), H == a(a).
cl(clause-6) :-
	clause(mtcl:tcl(H), user:a), H == a.


		 /*******************************
		 *	       RECORDS		*
		 *******************************/

mkterm(T) :-
	string_to_list(S, "hello"),
	current_prolog_flag(max_tagged_integer, X),
	BigNum is X * 3,
	NegBigNum is -X*5,
	T = term(atom,			% an atom
		 S,			% a string
		 1,			% an integer
		 BigNum,		% large integer
		 -42,			% small negative integer
		 NegBigNum,		% large negative integer
		 3.4,			% a float
		 _,			% a singleton
		 A, A,			% a shared variable
		 [a, list]).		% a list

erase_all(Key) :-
	recorded(Key, _, Ref),
	erase(Ref),
	fail.
erase_all(_).

record(recorda-1) :-
	erase_all(r1),
	mkterm(T0),
	recorda(r1, T0),
	recorded(r1, T1),
	T0 =@= T1.
record(recorda-2) :-
	erase_all(r2),
	mkterm(T0),
	recorda(r2, T0, Ref),
	recorded(K, T1, Ref),
	K == r2,
	T0 =@= T1.
record(recorda-3) :-
	erase_all(r3),
	\+ current_key(r3),
	recorda(r3, test),
	current_key(r3).
record(recorda-4) :-
	erase_all(r4),
	recorda(r4, aap),
	recorda(r4, noot),
	recordz(r4, mies),
	findall(X, recorded(r4, X), Xs),
	Xs = [noot, aap, mies].
record(recorda-5) :-
	recorda(bla,sign(a,(b,c),d)),
	\+ recorded(bla, sign(_,(B,B),_)),
	\+ (recorded(bla,S),
	    S=sign(_,(B,B),_)).
record(erase-1) :-
	erase_all(r5),
	recorda(r5, aap, R),
	recorda(r5, noot),
	erase(R),
	findall(X, recorded(r5, X), Xs),
	Xs = [noot].
record(erase-2) :-
	retractall(a(_)),
	assert(a(1), Ref),
	erase(Ref),
	findall(X, a(X), Xs),
	Xs = [].


		 /*******************************
		 *	       FLAGS		*
		 *******************************/

flag(arith-1) :-
	flag(f, Old, 0),
	flag(f, V, V+1),
	flag(f, NV, Old),
	NV == 1.
flag(arith-2) :-
	flag(f, Old, 100),
	flag(f, V, mean(V, 0)),
	flag(f, NV, Old),
	NV == 50.


		 /*******************************
		 *	    UPDATE-VIEW		*
		 *******************************/

:- dynamic
	a/1.

update(assert-1) :-
	retractall(a(_)),
	\+ ( assert(a(1)),
	     assert(a(2)),
	     a(X),
	     assert(a(3)),
	     X = 3
	   ).
update(retract-1) :-
	retractall(a(_)),
	(   assert(a(1)),
	    assert(a(2)),
	    retract(a(_)),
	    assert(a(3)),
	    fail
	;   findall(X, a(X), Xs),
	    Xs = [3,3]
	).
update(retract-2) :-
	retractall(a(_)),
	assert(a(1)),
	assert(a(2)),
	a(X),
	ignore(retract(a(2))),
	X = 2.


		 /*******************************
		 *	       CONTROL		*
		 *******************************/

softcut1(A) :-
	(   between(1, 2, A)
	*-> true
	;   A = 3
	).
softcut2(A) :-
	(   between(3, 2, A)
	*-> true
	;   A = 1
	).

do_block :-
	exit(notmyblock, ok).

bb(a) :-
	!(myblock).
bb(b).

b1 :- b2.
b1.

b2 :- exit(test, b).

b3 :- b4.
b3.

b4 :-
	!(test).

/* c*: tests for handling !
*/

c1 :-
	\+ ( true, !, fail ).

c2 :-
	(   true
	->  !, fail
	;   true
	).
c2.

c3 :-
	\+ (true, !, fail).

c4 :-					% ! in (A, ! -> B) must cut A
	\+ c4_body,
	flag(c4, 1, 0).
c4_body :-
	flag(c4, _, 0),
	(   c4(_), !,
	    flag(c4, X, X+1),
	    fail
	->  writeln('OOPS')
	).

c4(1).
c4(2).
c4(3).

/* test data for variable allocation in control-structures
*/

p(f(a,d)).
p(f(b,c)).

control(softcut-1) :-
	findall(A, softcut1(A), [1,2]).
control(softcut-2) :-
	findall(A, softcut2(A), [1]).
control(block-1) :-
	catch(block(myblock, do_block, _), E, true),
	E =@= error(existence_error(block, notmyblock), _).
control(block-2) :-
	block(notmyblock, do_block, X),
	X == ok.
control(block-3) :-
	\+ (   block(myblock, bb(X), _),
	       X == b
	   ).
control(block-4) :-
	block(test, b1, B),
	B == b,
	'$get_predicate_attribute'(b1, references, 0).
control(block-5) :-
	block(test, b3, _),
	'$get_predicate_attribute'(b3, references, 0).
control(cut-1) :-
	c1.
control(cut-2) :-
	\+ c2.
control(cut-3) :-
	c3.
control(cut-4) :-
	c4.
control(not-1) :-			% 2-nd call must generate FIRSTVAR
	( fail ; \+ \+ p(f(X,Y)) ), p(f(X,Y)).
control(not-2) :-			% see comments with compileBody()
	garbage_collect,		% may crash if wrong
	prolog_current_frame(F),
	prolog_frame_attribute(F, argument(4), Y),
	var(Y),				% additional test whether it is reset
	(   fail
	;   \+ A\=A
	).
control(ifthen-1) :-			% Must be the same
	( fail
	; ((p(f(X,Y))->fail;true)->fail;true)
	),
	p(f(X,Y)).


		 /*******************************
		 *	     EXCEPTIONS		*
		 *******************************/

do_exception_1 :-
	A = _,
	A.

rethrow(G) :-
	catch(G, E, throw(E)).

throwit :-
	throw(foo(_)).

catchme :-
	catch(throwit, _, true).

exception(call-1) :-
	catch(do_exception_1, E, true),
	E =@= error(instantiation_error, _).
exception(call-2) :-
	\+ catch(do_exception_1, _, fail).
exception(call-3) :-
	catch(rethrow(do_exception_1), E, true),
	E =@= error(instantiation_error, _).
exception(call-4) :-
	catch(throwit, foo(X), X = a),
	X = a.
exception(call-5) :-
	catch(throwit, _, catchme).

		 /*******************************
		 *	  RESOURCE ERRORS	*
		 *******************************/

choice.
choice.

local_overflow :-
	choice,
	local_overflow.

global_overflow(X) :-
	global_overflow(s(X)).


resource(stack-1) :-
	catch(local_overflow, E, true),
	E = error(resource_error(stack), local).
resource(stack-2) :-			% VERY slow with -DO_SECURE
	catch(global_overflow(0), E, true),
	E = error(resource_error(stack), global).


		 /*******************************
		 *	       GC		*
		 *******************************/

make_data(0, []) :- !.
make_data(N, s(X)) :-
	NN is N - 1,
	make_data(NN, X).

gc(shift-1) :-
	(   feature(dynamic_stacks, true)
	->  true
	;   MinFree is 400 * 1024,
	    stack_parameter(global, min_free, _, MinFree)
	).
gc(gc-1) :-
	garbage_collect.
gc(gc-2) :-			% Beautiful crash.  See compilation of \+
	\+( x(X,2) == x(X,1) ),
	garbage_collect,
	true.
gc(gc-3) :-
	\+ \+ ( gc_data(X),
		garbage_collect,
		X == a
	      ).
gc(agc-1) :-
	garbage_collect_atoms.
gc(agc-2) :-
	(   current_prolog_flag(agc_margin, Margin),
	    Margin > 0
	->  UpTo is Margin*2,
	    statistics(agc_gained, Gained0),
	    forall(between(0, UpTo, X), atom_concat(foobar, X, _)),
	    statistics(agc_gained, Gained1),
	    Gained is Gained1 - Gained0,
	    Gained > UpTo - 10		% might be some junk
	;   true			% no atom-gc
	).

gc_data(a).

		 /*******************************
		 *            FLOATS		*
		 *******************************/

floatconv(float-1) :-
	A is 5.5/5.5, integer(A).
floatconv(float-2) :-
	current_prolog_flag(max_integer, MI),
	ToHigh is MI + 10000,		% +1 may fail on 64-bit systems
	float(ToHigh).
floatconv(float-3) :-
	(   current_prolog_flag(max_integer, 2147483647)
	->  term_to_atom(X, 2147483648)
	;   current_prolog_flag(max_integer, 9223372036854775807)
	->  term_to_atom(X, 9223372036854775808)
	),
	float(X).


		 /*******************************
		 *	ATTRIBUTED VARIABLES	*
		 *******************************/

test:attr_unify_hook(_Att, _Val).

u_predarg(predarg).
u_termarg(f(termarg)).
u_nil([]).
u_list([a]).
u_args(X, X).

u(predarg, X) :-
	u_predarg(X).
u(termarg, X) :-
	u_termarg(f(X)).
u(nil, X) :-
	u_nil(X).
u(list, X) :-
	u_list(X).
u(unify, X) :-
	X = unify.
u(arith, X) :-
	X is 3.
u(args, X) :-
	u_args(X, args).

test_wakeup(How) :-
	freeze(X, Y=ok),
	u(How, X),
	Y == ok.

avar(access-1) :-				% very basic access
	put_attr(X, test, hello),
	get_attr(X, test, H),
	H == hello.
avar(backtrack-1) :-			% test backtracking
	retractall(mark(_)),
	put_attr(X, test, hello),
	(   put_attr(X, test, world),
	    get_attr(X, test, A),
	    A == world,
	    assert(mark(1)),		% point must be reached
	    fail
	;   get_attr(X, test, A),
	    A == hello
	),
	retract(mark(1)).
avar(rec-1) :-
	put_attr(X, test, hello),
	recorda(x, x(X,X), Ref),
	recorded(_, x(A,B), Ref),
	erase(Ref),
	A == B.
avar(rec-2) :-
	put_attr(X, test, hello),
	recorda(x, X, Ref),
	recorded(_, Y, Ref),
	erase(Ref),
	var(Y),
	get_attr(Y, test, A),
	A == hello.
avar(wakeup-1) :-
	test_wakeup(predarg).
avar(wakeup-2) :-
	test_wakeup(termarg).
avar(wakeup-3) :-
	test_wakeup(nil).
avar(wakeup-4) :-
	test_wakeup(unify).
avar(wakeup-5) :-
	test_wakeup(arith).
avar(wakeup-6) :-
	test_wakeup(args).
avar(type-1) :-
	put_attr(X, test, a),
	avar(X).
avar(type-2) :-
	put_attr(X, test, a),
	var(X).
avar(type-3) :-
	put_attr(X, test, a),
	\+ nonvar(X).
avar(type-4) :-
	put_attr(X, test, a),
	\+ ground(X).
avar(type-5) :-
	put_attr(X, test, a),
	\+ atomic(X).


		 /*******************************
		 *	     COPY-TERM		*
		 *******************************/

copy_term(rct-1) :-
	copy_term(a, X), X == a.
copy_term(rct-2) :-
	copy_term(X, Y), X \== Y.
copy_term(rct-3) :-
	copy_term(f(a), Y), Y == f(a).
copy_term(rct-4) :-
	copy_term(f(X), Y), Y = f(Z), X \== Z.
copy_term(rct-5) :-
	copy_term(f(X, X), Y), Y = f(A,B), A == B.
copy_term(rct-6) :-
	X = f(X),
	copy_term(X, Y),
	X = Y.
copy_term(ct-1) :-
	T = (A=foo(bar(A), y:x(A, b, c))),
	copy_term(T, B),
	numbervars(B, 0, _),
	\+ ground(T).
copy_term(av-1) :-			% copy attributed variables
	X = foo(V),
	put_attr(V, test, y),
	copy_term(X, Y),
	Y = foo(Arg),
	get_attr(Arg, test, A),
	A == y,
	put_attr(Arg, test, z),
	get_attr(V, test, y).
copy_term(av-2) :-
	X = foo(V,V),
	put_attr(V, test, y),
	copy_term(X, Y),
	Y = foo(A,B),
	A == B,
	get_attr(A, test, y).
copy_term(av-3) :-
	put_attr(X, test, f(X)),
	copy_term(X, Y),
	get_attr(Y, test, A),
	A = f(Z),
	Y == Z.
copy_term(av-3) :-
	freeze(X, true),
	freeze(X, Done = true),
	copy_term(X, Y),
	X = ok,
	Done == true,
	get_attr(Y, freeze, (true, D2=true)),
	var(D2),
	Y = ok,
	D2  == true.


		 /*******************************
		 *    BIG TERMS, ATOM-TO-TERM	*
		 *******************************/

s(0, 0) :- !.
s(N, s(S)) :-
	NN is N - 1,
	s(NN, S).

termtest(N) :-
	s(N, S),
	term_to_atom(S, A),
	atom_length(A, L),
	L =:= 3*N+1,
	atom_to_term(A, S2, []),
	S == S2.

term_atom(term_to_atom-1) :-
	termtest(10).
term_atom(term_to_atom-2) :-
	termtest(1000).


		 /*******************************
		 *		I/O		*
		 *******************************/

io(tell-1) :-
	tell(test_x),
	format('~q.~n', [a]),
	tell(test_y),
	format('~q.~n', [b]),
	tell(test_x),
	format('~q.~n', [c]),
	told,
	tell(test_y),
	told,
	read_file_to_terms(test_x, [a,c], []),
	read_file_to_terms(test_y, [b], []),
	delete_file(test_x),
	delete_file(test_y),
	\+ stream_property(_, file_name(test_x)),
	\+ stream_property(_, file_name(test_y)).

io(tell-2) :-
	current_output(OrgOut),
	open_null_stream(Out),
	set_output(Out),
	write(Out, x),
	telling(Old), tell(test_y),
	format('~q.~n', [b]),
	told, tell(Old),
	write(Out, y),
	flush_output(Out),
	character_count(Out, 2),
	close(Out),
	set_output(OrgOut),
	read_file_to_terms(test_y, [b], []),
	delete_file(test_y).


		 /*******************************
		 *	       POPEN		*
		 *******************************/

popen(pwd-1) :-
	open(pipe(pwd), read, Fd),
	collect_line(Fd, String),
	close(Fd),
	atom_codes(Pwd, String),
	same_file(Pwd, '.').
popen(cat-1) :-
	open(pipe('cat > .pltest'), write, Fd),
	format(Fd, 'Hello World', []),
	close(Fd),
	open('.pltest', read, Fd2),
	collect_data(Fd2, String),
	close(Fd2),
	delete_file('.pltest'),
	atom_codes(A, String),
	A == 'Hello World'.
popen(cat-2) :-
	absolute_file_name(swi('library/MANUAL'), Manual),
	open(Manual, read, Fd),
	open(pipe(true), write, Pipe),
	catch(copy_stream_data(Fd, Pipe),
	      E,
	      true),
	close(Fd),
	catch(close(Pipe), _, true),	% ???
	(   var(E)
	->  format(user_error, 'No exception?~n', []),
	    fail
					% if signalling is enabled
	;   E = error(signal(pipe, _), context(copy_stream_data/2, _))
	->  true
					% otherwise
	;   E = error(io_error(write, _), context(_, 'Broken pipe'))
	->  true
	;   format(user_error, 'Wrong exception: ~p~n', [E]),
	    fail
	).

collect_line(Fd, String) :-
	get0(Fd, C0),
	collect_line(C0, Fd, String).

collect_line(-1, _, []) :- !.
collect_line(10, _, []) :- !.
collect_line(13, _, []) :- !.
collect_line(C, Fd, [C|T]) :-
	get0(Fd, C2),
	collect_line(C2, Fd, T).

collect_data(Fd, String) :-
	get0(Fd, C0),
	collect_data(C0, Fd, String).

collect_data(-1, _, []) :- !.
collect_data(C, Fd, [C|T]) :-
	get0(Fd, C2),
	collect_data(C2, Fd, T).


		 /*******************************
		 *	      TIMEOUT		*
		 *******************************/

timeout(pipe-1) :-
	(   current_prolog_flag(pipe, true)
	->  open(pipe('echo xx && sleep 2 && echo xx.'), read, In),
	    set_stream(In, timeout(1)),
	    wait_for_input([In], [In], infinite),
	    catch(read(In, _), E1, true),
	    E1 = error(timeout_error(read, _), _),
	    wait_for_input([In], [In], infinite),
	    catch(read(In, Term), E2, true),
	    var(E2),
	    Term == xx,
	    close(In)
	;   true
	).


		 /*******************************
		 *	      FILES		*
		 *******************************/

:- dynamic
	testfile/1.
:- initialization
	prolog_load_context(file, File),
	assert(testfile(File)).

file(exists-1) :-
	\+ exists_file(foobar26).
file(exists-2) :-
	testfile(File),
	exists_file(File).
file(exists-3) :-
	\+ exists_file('.').
file(dir-1) :-
	exists_directory('.').
file(dir-2) :-
	testfile(File),
	\+ exists_directory(File).


		 /*******************************
		 *	   CODE/CHAR-TYPE	*
		 *******************************/

ctype(code_type-1) :-
	code_type(97, to_lower(97)),
	code_type(97, to_lower(65)).
ctype(code_type-2) :-
	findall(X, code_type(X, lower), Lower),
	subset("abcdefghijklmnopqrstuvwxyz", Lower).
ctype(code_type-3) :-
	code_type(48, digit(0)).
ctype(code_type-4) :-
	code_type(X, digit(0)),
	X == 48.
ctype(code_type-5) :-
	code_type(48, digit(W)),
	W == 0.

		 /*******************************
		 *	      CONSULT		*
		 *******************************/

mk_include :-
	open('test_included.pl', write, Out1),
	format(Out1, ':- dynamic foo/1.\n', []),
	close(Out1),

	open('test_include.pl', write, Out2),
	format(Out2, ':- include(test_included).\n', []),
	format(Out2, 'foo(a).\n', []),
	close(Out2).

load_program(include-1) :-
	mk_include,
	abolish(foo, 1),
	load_files(test_include, [silent(true)]),
	assert(foo(b)),
	findall(X, retract(foo(X)), [a,b]),
	delete_file('test_included.pl'),
	delete_file('test_include.pl').


		 /*******************************
		 *	    THREADING		*
		 *******************************/

:- dynamic
	th_data/1,
        at_exit_called/0.

at_exit_work :-
        thread_at_exit(assert(at_exit_called)),
        thread_exit(true).

th_do_something :-
	forall(between(1, 5, X),
	       assert(th_data(X))).
th_check_done :-
	findall(X, retract(th_data(X)), [1,2,3,4,5]).

thread(join-1) :-
	thread_create(th_do_something, Id, []),
	thread_join(Id, Exit),
	Exit == true,
	th_check_done.
thread(message-1) :-
	thread_self(Me),
	thread_create(thread_send_message(Me, hello), Id, []),
	thread_get_message(hello),
	thread_join(Id, true).
thread(signal-1) :-
	thread_create((repeat, fail), Id, []),
	thread_signal(Id, throw(stopit)),
	thread_join(Id, Exit),
	Exit == exception(stopit).
thread(at_exit-1) :-
        retractall(at_exit_called),
        thread_create(at_exit_work, Id, []),
        thread_join(Id, exited(true)),
	retract(at_exit_called).
	

		 /*******************************
		 *	      SCRIPTS		*
		 *******************************/


:- dynamic
	script_dir/1.

set_script_dir :-
	script_dir(_), !.
set_script_dir :-
	find_script_dir(Dir),
	assert(script_dir(Dir)).

find_script_dir(Dir) :-
	prolog_load_context(file, File),
	follow_links(File, RealFile),
	file_directory_name(RealFile, Dir).

follow_links(File, RealFile) :-
	read_link(File, _, RealFile), !.
follow_links(File, File).


:- set_script_dir.

run_test_script(Script) :-
	file_base_name(Script, Base),
	file_name_extension(Pred, _, Base),
	load_files(Script, [silent(true)]),
	Pred.

run_test_scripts(Directory) :-
	(   script_dir(ScriptDir),
	    concat_atom([ScriptDir, /, Directory], Dir),
	    exists_directory(Dir)
	->  true
	;   Dir = Directory
	),
	atom_concat(Dir, '/*.pl', Pattern),
	expand_file_name(Pattern, Files),
	file_base_name(Dir, BaseDir),
	format('Running scripts from ~w ', [BaseDir]), flush,
	run_scripts(Files),
	format(' done~n').

run_scripts([]).
run_scripts([H|T]) :-
	(   catch(run_test_script(H), Except, true)
	->  (   var(Except)
	    ->  put(.), flush
	    ;   Except = blocked(Reason)
	    ->  assert(blocked(H, Reason)),
		put(!), flush
	    ;   script_failed(H, Except)
	    )
	;   script_failed(H, fail)
	),
	run_scripts(T).

script_failed(File, fail) :-
	format('~NScript ~w failed~n', [File]).
script_failed(File, Except) :-
	message_to_string(Except, Error),
	format('~NScript ~w failed: ~w~n', [File, Error]).


		 /*******************************
		 *        TEST MAIN-LOOP	*
		 *******************************/

testset(syntax).
testset(unify).
testset(arithmetic).
testset(arithmetic_functions).
testset(floattest).
testset(chars).
testset(depth_limit) :-
	current_predicate(_, user:call_with_depth_limit(_,_,_)).
testset(type_test).
testset(meta).
testset(avar).
testset(copy_term).
testset(cleanup).
testset(term).
testset(list).
testset(sets).
testset(atom_handling).
testset(string_handling).
testset(proc).
testset(cl).
testset(record).
testset(flag).
testset(update).
testset(gc).
testset(floatconv) :-
	current_prolog_flag(iso, false).
testset(control).
testset(exception).
testset(term_atom).
testset(io).
testset(popen) :-
	current_prolog_flag(pipe, true).
testset(timeout).
testset(file).
testset(load_program).
testset(ctype).
testset(thread) :-
	current_prolog_flag(threads, true).
testset(resource).

testdir('Tests/thread') :-
	current_prolog_flag(threads, true).

:- dynamic
	failed/1,
	blocked/2.

test :-
	retractall(failed(_)),
	retractall(blocked(_,_)),
	forall(testset(Set), runtest(Set)),
	scripts,
	report_blocked,
	report_failed.

scripts :-
	forall(testdir(Dir), run_test_scripts(Dir)).


report_blocked :-
	findall(Head-Reason, blocked(Head, Reason), L),
	(   L \== []
        ->  format('~nThe following tests are blocked:~n', []),
	    (	member(Head-Reason, L),
		format('    ~p~t~40|~w~n', [Head, Reason]),
		fail
	    ;	true
	    )
        ;   true
	).
report_failed :-
	findall(X, failed(X), L),
	length(L, Len),
	(   Len > 0
        ->  format('~n*** ~w tests failed ***~n', [Len]),
	    fail
        ;   format('~nAll tests passed~n', [])
	).

runtest(Name) :-
	format('Running test set "~w" ', [Name]),
	flush,
	functor(Head, Name, 1),
	nth_clause(Head, _N, R),
	clause(Head, _, R),
	(   catch(Head, Except, true)
	->  (   var(Except)
	    ->  put(.), flush
	    ;   Except = blocked(Reason)
	    ->  assert(blocked(Head, Reason)),
		put(!), flush
	    ;   test_failed(R, Except)
	    )
	;   test_failed(R, fail)
	),
	fail.
runtest(_) :-
	format(' done.~n').
	
test_failed(R, Except) :-
	clause(Head, _, R),
	functor(Head, Name, 1),
	arg(1, Head, TestName),
	clause_property(R, line_count(Line)),
	clause_property(R, file(File)),
	(   Except == fail
	->  format('~N~w:~d: Test ~w(~w) failed~n',
		   [File, Line, Name, TestName])
	;   message_to_string(Except, Error),
	    format('~N~w:~d: Test ~w(~w):~n~t~8|ERROR: ~w~n',
		   [File, Line, Name, TestName, Error])
	),
	assert(failed(Head)).

blocked(Reason) :-
	throw(blocked(Reason)).

