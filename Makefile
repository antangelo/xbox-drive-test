NXDK_DIR = $(CURDIR)/../nxdk
NXDK_NET = y

XBE_TITLE = xbox-drive-test
GEN_XISO = $(XBE_TITLE).iso
SRCS = $(wildcard $(CURDIR)/*.c)

CFLAGS += -I$(CURDIR)
CFLAGS += -I$(CURDIR)/lib

include $(NXDK_DIR)/Makefile
