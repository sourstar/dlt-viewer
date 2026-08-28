# Credits — performance fork

This branch is a performance-focused fork of
[COVESA/dlt-viewer](https://github.com/COVESA/dlt-viewer), branched from
`master` at `edf7aa5` ("Plugin pane filename in sync with plugin widget
filename (#812)").

Everything in the upstream project keeps its original authorship and its
MPL-2.0 licence. This file records where the non-upstream changes came from.

## Work merged from other contributors

### gernotmuc — filter matching hot paths

Source: [`gernotmuc/dlt-viewer`](https://github.com/gernotmuc/dlt-viewer),
branch `fix/filter-load-hotpath-public` (commits `2c634a5`, `22f981c`,
`9fb3969`). Not raised as an upstream pull request at the time of writing.

Memoizes the rendered header and payload strings inside `QDltMsg` so they are
built once per message instead of once per filter, adds `*Ref()` accessors
that avoid `QString` copies in `QDltFilter::match()`, and stable-sorts the
positive and negative filter lists cheapest-first in `updateSortedFilter()`.

Integrated here as commit "Speed up filter matching hot paths", squashed from
the three upstream commits (which were successive whole-file uploads of the
same change) with the original author preserved.

### gernotmuc — metadata-only prefilter

Source: same fork, branch `fix/filter-prefilter-public-20260611` (commits
`884909e`, `4da7c43`, `44c4b69`). Also not raised as an upstream PR.

Adds `QDltFilterList::checkFilterBeforeDecode()`, which answers
Match / Reject / NeedsDecode from raw message metadata without touching the
payload, and uses the answer in the filter indexer to skip plugin decoding for
messages that are already rejected.

Integrated here as commit "Add metadata-only prefilter decision before decode".
The anonymous-namespace conflict against the hot-path change was resolved by
keeping both helper blocks. Extended afterwards — see below.

### gernotmuc — related upstream PR

[COVESA/dlt-viewer#818](https://github.com/COVESA/dlt-viewer/pull/818)
("Improve viewer core stability and search performance") is the same author's
stability and search work. It is **not** merged here: it is an open upstream
PR and will arrive through the normal upstream merge.

## Work added in this fork

Authored here, building on the above:

* **Completed the prefilter for the apply-filter path.** The upstream branch
  only ran `checkFilterBeforeDecode()` in `modeIndexAndFilter` (initial load).
  Applying a filter to an already-indexed file uses `modeFilter`, which is the
  case users actually wait on. Since every other step in `processMessage()` is
  gated on `modeIndexAndFilter`, the decoded message has a single consumer in
  that mode and the decode skip does not need the "no viewer plugins"
  precondition the initial-load path requires.
* **Bulk index-cache I/O.** `saveIndex()`/`loadIndex()` transferred one
  `qint64` per `QFile` call, and the load asked `QFile::pos()` once per 8
  bytes for its progress bar. Both now work in 4 MB chunks, and the load is
  cancellable.
* **Per-segment indexing progress.** The scan's cancellation check and
  progress calculation moved out of the inner byte loop, where they ran about
  a dozen times per message and called `QFile::pos()` each time.
* **Dropped the eager second file pass.** `QDltFile::createIndex()` called
  `calculateTotalSizes()`, a full extra pass over every message, to populate
  counters whose only consumer is the "DLT File Size" dialog. The getters
  already compute them lazily.
* **Removed an O(n²).** `addFilterIndex()` re-merged the whole filter index on
  every appended message whenever a manual marker was set.
* **Export without decoding.** `QDltExporter` parsed every message even for a
  plain binary DLT export, where the original bytes are written straight back
  out.
* **Cheaper marker counting.** Recounting no longer materialises an identity
  index vector (~80 MB on a 10M-message file) just to mean "all messages".
* **Container build and cross-compilation fixes.** See `docker/README.md`.

## Deliberately not merged

* **The upstream "delink" series** —
  [#821](https://github.com/COVESA/dlt-viewer/pull/821),
  [#823](https://github.com/COVESA/dlt-viewer/pull/823),
  [#825](https://github.com/COVESA/dlt-viewer/pull/825), #826, #827 by
  Renu-Priya411 and Pavithra-aa-Anand. This is the architectural fix for the
  UI freezes: it moves indexing, filtering, search and decode onto worker
  threads. It is also ~3,000 lines across five open PRs with unresolved review
  comments, and it rewrites the same files this fork touches. Merging a moving
  WIP refactor into a fork would produce conflicts on every upstream update
  and bury the targeted changes above. Track it upstream instead.
* **shubhamshahaBMW's WIP** — [#759](https://github.com/COVESA/dlt-viewer/pull/759)
  (filter indexing), #757 (sorting index), #767 (filter index cache). All
  marked WIP, all conflicting with current master, last touched May 2026.

## Known remaining bottlenecks

Identified but not addressed here, in rough order of expected payoff:

1. **Per-message `seek()` + small `read()` under one global mutex**
   (`QDltFile::getMsg`). Every full-file pass performs one I/O round trip per
   message where a sequential block stream would do, and the single
   `mutexQDlt` serialises the parallel Find-All. This is the largest remaining
   win and the deepest change; it wants a profiler and real DLT files first.
2. **`DltFileIndexer::stop()` blocks the GUI thread** with an unbounded
   `wait()`. Cancellation is now much more responsive because the loops that
   previously ignored `stopFlag` honour it, but the call is still synchronous.
   The delink series fixes this properly.
3. **Marker recounting still runs on the GUI thread**, kept alive by
   `processEvents()`.
4. **`QDltMsg::setMsg()` always decodes arguments**; there is no headers-only
   parse mode.
