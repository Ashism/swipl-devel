# Declarations that can be shared between the Unix and
# Windows Makefiles

# Dialect library files

DIALECT=yap.pl hprolog.pl commons.pl ciao.pl sicstus.pl
YAP=	README.TXT
SICSTUS=block.pl timeout.pl system.pl arrays.pl lists.pl \
	sockets.pl swipl-lfr.pl
CIAO=	assertions.pl isomodes.pl regtypes.pl sockets.pl \
	read.pl write.pl strings.pl format.pl lists.pl \
	terms.pl system.pl iso_misc.pl aggregates.pl \
	classic.pl
CIAO_ENGINE=internals.pl hiord_rt.pl
ISO=	iso_predicates.pl
