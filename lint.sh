#!/usr/bin/env bash
clang-tidy ./include/* src/* -- -I./include
exit $?
