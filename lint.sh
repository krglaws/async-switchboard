#!/usr/bin/env bash
clang-tidy ./include/* src/* tests/* -- -I./include
exit $?
