################################################################################
#
# u-dma-buf
#
################################################################################

U_DMA_BUF_VERSION = v4.5.0
U_DMA_BUF_SITE = $(call github,ikwzm,udmabuf,$(U_DMA_BUF_VERSION))

U_DMA_BUF_MODULE_MAKE_OPTS += CONFIG_U_DMA_BUF=m

$(eval $(kernel-module))
$(eval $(generic-package))
