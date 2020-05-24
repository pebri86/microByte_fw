#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := micro-gnuboy

EXTRA_COMPONENT_DIRS := components/drivers/spi \
						components/drivers/system_configuration \
						components/drivers/st7789 \
						components/drivers/display_hal \
						components/drivers/user_input \
						components/gnuboy_core


include $(IDF_PATH)/make/project.mk