# Copyright (c) 2025 Group 8
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SENSOR_NODE sensor_node.c)
