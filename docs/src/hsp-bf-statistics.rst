HSP Bloom filter diagnostics
============================

The ``bloom_filter`` implementation reports two diagnostics in addition to
Bloom filter queries and false positives:

* ``bf_counter_saturations`` counts increment attempts clipped because the
  selected counter is already at its maximum (255 for 8-bit counters, 15 for 4-bit counters).
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
The legacy synchronous path counts each lookup invocation, including retries.
The internal pipeline counts new transactions; merged waiters and downstream
backpressure do not repeat the BF stage.

Each filter maintains an exact reference-counted membership table solely as a
software diagnostic oracle. It follows insertions and deletions, survives ROI
statistics resets with live filter contents, and does not affect query decisions,
promotion, replacement, or simulated latency. Its host memory and runtime overhead
must not be included in modeled hardware area. A detected false negative still
takes the normal miss path; the oracle never supplies a translation.

See :doc:`hsp-counter-widths` for compiled counter widths and internal latency.
HSP hotness distribution statistics are unchanged.
