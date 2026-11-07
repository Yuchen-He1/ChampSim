HSP Bloom filter diagnostics
============================

The ``bloom_filter`` implementation reports two diagnostics in addition to
Bloom filter queries and false positives:

* ``bf_counter_saturations`` counts increment attempts clipped because the
  selected counter is already at its maximum (currently 255 for 8-bit counters).
  Reaching the maximum without clipping does not count. Each hash update counts
  separately, including hashes that alias the same counter.
* ``bf_false_negatives`` counts HSP lookups that fail to return a resident
  translation because its buffer's Bloom filter is negative. Each lookup counts
  at most once. A successful hit in another buffer suppresses the count. If an
  unrelated buffer is positive while the actual resident is hidden by a negative
  filter, the lookup counts as a false negative, not a false positive.

These underscore-separated names appear in the top-level JSON as sums of ROI
cache statistics. Per-cache phase JSON uses ``bf counter saturations`` and
``bf false negatives``. Both statistics also appear in the text output for
configured filters, reset at phase boundaries, and support statistics subtraction.
Query retries follow the existing ``bf_queries`` accounting convention: each
invocation of the HSP lookup is a separate observation.

Each filter maintains an exact reference-counted membership table solely as a
software diagnostic oracle. It follows insertions and deletions, survives ROI
statistics resets with live filter contents, and does not affect query decisions,
promotion, replacement, or simulated latency. Its host memory and runtime overhead
must not be included in modeled hardware area. A detected false negative still
takes the normal miss path; the oracle never supplies a translation.

This change does not add configurable counter widths or modify counter saturation
semantics. HSP hotness statistics are unchanged.
