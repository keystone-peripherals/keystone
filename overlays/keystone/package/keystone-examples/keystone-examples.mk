################################################################################
#
# Keystone examples
#
################################################################################

ifeq ($(KEYSTONE_EXAMPLES),)
$(error KEYSTONE_EXAMPLES directory not defined)
else
KEYSTONE_EXAMPLES_IGNORE_DIRS = */cmake-build-debug* */.idea*
include $(KEYSTONE)/mkutils/pkg-keystone.mk
endif

KEYSTONE_EXAMPLES_DEPENDENCIES += host-keystone-sdk keystone-runtime xz

# Required to build enclaved ML accelerators
ifneq ($(BR2_EXTERNAL_TVM_PATH),)
KEYSTONE_EXAMPLES_DEPENDENCIES += tvm host-tvm #host-python-tvm host-python-vta
KEYSTONE_EXAMPLES_CONF_OPTS += -DBUILDROOT_HOST_DIR=$(HOST_DIR) -DBUILDROOT_TARGET_DIR=$(TARGET_DIR)

# We take a bit of a non-buildroot approach here. We need several python packages in order to
# retrieve pretrained machine learning models. The primary package used here (in conjunction with
# TVM) is mxnet, also from Apache. However, building mxnet is ... a bit of a pain. Its basically a
# whole new machine learning library ala pytorch, and as such induces a bunch of dependencies on
# BLAS libraries, takes a huge amount of time to compile, etc. Therefore, instead of going through
# this, we just pip install the necessary packages. This is very much a nonstandard approach in
# Buildroot, which typically expects us to build everything from source. May god forgive our sins.

define KEYSTONE_EXAMPLES_PIP_INSTALL_DEPS
	$(HOST_DIR)/bin/python -m ensurepip
	$(HOST_DIR)/bin/python -m pip install mxnet scipy attrs
	sed -i 's/bool = onp.bool/bool = onp.bool_/g' \
          $(HOST_DIR)/lib/python$(PYTHON3_VERSION_MAJOR)/site-packages/mxnet/numpy/utils.py
endef

# However, this doesn't actually work yet (thanks LLVM) -- we leave enabling this as a todo
KEYSTONE_EXAMPLES_POST_CONFIGURE_HOOKS += #KEYSTONE_EXAMPLES_PIP_INSTALL_DEPS

# Point to the VTA hardware configuration in the target directory
KEYSTONE_EXAMPLES_MAKE_ENV += VTA_HW_PATH=$(TARGET_DIR)/usr/share/vta/

endif

KEYSTONE_EXAMPLES_CONF_OPTS += -DKEYSTONE_SDK_DIR=$(HOST_DIR)/usr/share/keystone/sdk \
                                -DKEYSTONE_EYRIE_RUNTIME=$(KEYSTONE_RUNTIME_BUILDDIR)

KEYSTONE_EXAMPLES_MAKE_ENV += KEYSTONE_SDK_DIR=$(HOST_DIR)/usr/share/keystone/sdk
KEYSTONE_EXAMPLES_MAKE_OPTS += examples

# Install only .ke files
define KEYSTONE_EXAMPLES_INSTALL_TARGET_CMDS
	find $(@D) -name '*.ke' | \
                xargs -i{} $(INSTALL) -D -m 755 -t $(TARGET_DIR)/usr/share/keystone/examples/ {}
endef

$(eval $(keystone-package))
$(eval $(cmake-package))
