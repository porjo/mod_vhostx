#
# Copyright (c) 2005 Xavier Beaudouin <kiwi@oav.net>
#

##
## Have a look in this file the compilation / configuration options.
##

APXS = apxs

NAME = mod_vhostx
SRCS = mod_vhostx.c
OBJS = mod_vhostx.o
APACHE_MODULE = $(NAME).so

RM = rm -f

CFLAGS= -I/usr/include/php -I/usr/include/php/Zend -I/usr/include/php/TSRM

CFLAGS+= -DVH_DEBUG 
CFLAGS+= -DHAVE_MOD_PHP_SUPPORT 
CFLAGS+= -DHAVE_MPM_ITK_SUPPORT


################################################################
### End of user configuration directives
################################################################

default: build

all: build install

build: $(SRCS)
	$(APXS) -c $(LDFLAGS) $(CFLAGS) $(SRCS)

install:
	strip .libs/mod_vhostx.so
	$(APXS) -i mod_vhostx.la

clean:
	$(RM) $(OBJS) $(APACHE_MODULE) *.slo *.la *.lo 
	$(RM) -r .libs

