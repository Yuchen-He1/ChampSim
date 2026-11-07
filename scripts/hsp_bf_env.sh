#!/usr/bin/env sh
# Edit these values, then SOURCE this file before running run.py or a binary:
#   . ./bf_env.sh
# Sourcing explicitly replaces the four BF settings in the current shell.

# Number of derived hash indices per query/insert/delete (double hashing).
export CHAMPSIM_STLB_HSP_BF_HASHES=7

# Number of counters PER filter; this is not the counter bit width.
export CHAMPSIM_STLB_HSP_BF_COUNTERS=4912

# Target FP parameter; explicit counter/hash settings determine actual FP.
export CHAMPSIM_STLB_HSP_BF_FALSE_POSITIVE_RATE=0.01

# BF stage delay in STLB clock cycles; zero is allowed.
export CHAMPSIM_STLB_HSP_BF_LATENCY=1

# Counter width is compiled into the selected binary and is not set here.
