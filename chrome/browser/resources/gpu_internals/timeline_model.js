// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


/**
 * @fileoverview TimelineModel is a parsed representation of the
 * TraceEvents obtained from base/trace_event in which the begin-end
 * tokens are converted into a hierarchy of processes, threads,
 * subrows, and slices.
 *
 * The building block of the model is a slice. A slice is roughly
 * equivalent to function call executing on a specific thread. As a
 * result, slices may have one or more subslices.
 *
 * A thread contains one or more subrows of slices. Row 0 corresponds to
 * the "root" slices, e.g. the topmost slices. Row 1 contains slices that
 * are nested 1 deep in the stack, and so on. We use these subrows to draw
 * nesting tasks.
 *
 */
cr.define('gpu', function() {
  /**
   * A TimelineSlice represents an interval of time on a given thread
   * associated with a specific trace event. For example,
   *   TRACE_EVENT_BEGIN1("x","myArg", 7) at time=0.1ms
   *   TRACE_EVENT_END()                  at time=0.3ms
   * Results in a single timeline slice from 0.1 with duration 0.2.
   *
   * All time units are stored in milliseconds.
   * @constructor
   */
  function TimelineSlice(title, colorId, start, args) {
    this.title = title;
    this.start = start;
    this.colorId = colorId;
    this.args = args;
    this.subSlices = [];
  }

  TimelineSlice.prototype = {
    selected: false,

    duration: undefined,

    get end() {
      return this.start + this.duration;
    }
  };

  /**
   * A TimelineThread stores all the trace events collected for a particular
   * thread. We organize the slices on a thread by "subrows," where subrow 0
   * has all the root slices, subrow 1 those nested 1 deep, and so on. There
   * is also a set of non-nested subrows.
   *
   * @constructor
   */
  function TimelineThread(parent, tid) {
    this.parent = parent;
    this.tid = tid;
    this.subRows = [[]];
    this.nonNestedSubRows = [];
  }

  TimelineThread.prototype = {
    getSubrow: function(i) {
      while (i >= this.subRows.length)
        this.subRows.push([]);
      return this.subRows[i];
    },

    addNonNestedSlice: function(slice) {
      for (var i = 0; i < this.nonNestedSubRows.length; i++) {
        var currSubRow = this.nonNestedSubRows[i];
        var lastSlice = currSubRow[currSubRow.length - 1];
        if (slice.start >= lastSlice.start + lastSlice.duration) {
          currSubRow.push(slice);
          return;
        }
      }
      this.nonNestedSubRows.push([slice]);
    },

    updateBounds: function() {
      var slices = this.subRows[0];
      if (slices.length != 0) {
        this.minTimestamp = slices[0].start;
        this.maxTimestamp = slices[slices.length - 1].end;
      } else {
        this.minTimestamp = undefined;
        this.maxTimestamp = undefined;
      }
    }

  };

  /**
   * The TimelineProcess represents a single process in the
   * trace. Right now, we keep this around purely for bookkeeping
   * reasons.
   * @constructor
   */
  function TimelineProcess(pid) {
    this.pid = pid;
    this.threads = {};
  };

  TimelineProcess.prototype = {
    getThread: function(tid) {
      if (!this.threads[tid])
        this.threads[tid] = new TimelineThread(this, tid);
      return this.threads[tid];
    }
  };

  /**
   * Builds a model from an array of TraceEvent objects.
   * @param {Array} events An array of TraceEvents created by
   *     TraceEvent.ToJSON().
   * @constructor
   */
  function TimelineModel(events) {
    this.processes = {};

    if (events)
      this.importEvents(events);
  }

  TimelineModel.prototype = {
    __proto__: cr.EventTarget.prototype,

    getProcess: function(pid) {
      if (!this.processes[pid])
        this.processes[pid] = new TimelineProcess(pid);
      return this.processes[pid];
    },

    /**
     * The import takes an array of json-ified TraceEvents and adds them into
     * the TimelineModel as processes, threads, and slices.
     */
    importEvents: function(events) {
      // A ptid is a pid and tid joined together x:y fashion, eg 1024:130
      // The ptid is a unique key for a thread in the trace.


      // Threadstate
      const numColorIds = 30;
      function ThreadState(tid) {
        this.openSlices = [];
        this.openNonNestedSlices = {};
      }
      var threadStateByPTID = {};

      var nameToColorMap = {};
      function getColor(name) {
        if (!(name in nameToColorMap)) {
          // Compute a simplistic hashcode of the string so we get consistent
          // coloring across traces.
          var hash = 0;
          for (var i = 0; i < name.length; ++i)
            hash = (hash + 37 * hash + 11 * name.charCodeAt(i)) % 0xFFFFFFFF;
          nameToColorMap[name] = hash % numColorIds;
          console.log(name, nameToColorMap[name]);
        }
        return nameToColorMap[name];
      }

      // Walk through events
      for (var eI = 0; eI < events.length; eI++) {
        var event = events[eI];
        var ptid = event.pid + ':' + event.tid;
        if (!(ptid in threadStateByPTID)) {
          threadStateByPTID[ptid] = new ThreadState();
        }
        var state = threadStateByPTID[ptid];
        if (event.ph == 'B') {
          var colorId = getColor(event.name);
          var slice = new TimelineSlice(event.name, colorId, event.ts,
                                        event.args);
          if (event.args['ui-nest'] === '0') {
            var sliceID = event.name;
            for (var x in event.args) {
              sliceID += ';' + event.args[x];
            }
            if (state.openNonNestedSlices[sliceID])
              console.log('Event ' + sliceID + ' already open.');
            state.openNonNestedSlices[sliceID] = slice;
          } else
            state.openSlices.push(slice);
        } else if (event.ph == 'E') {
          if (event.args['ui-nest'] === '0') {
            var sliceID = event.name;
            for (var x in event.args) {
              sliceID += ';' + event.args[x];
            }
            var slice = state.openNonNestedSlices[sliceID];
            if (!slice)
              continue;
            slice.duration = event.ts - slice.start;

            // Store the slice in a non-nested subrow.
            var thread = this.getProcess(event.pid).getThread(event.tid);
            thread.addNonNestedSlice(slice);
            delete state.openNonNestedSlices[name];
          } else {
            if (state.openSlices.length == 0) {
              // Ignore E events that that are unmatched.
              continue;
            }
            var slice = state.openSlices.pop();
            slice.duration = event.ts - slice.start;

            // Store the slice on the right subrow.
            var thread = this.getProcess(event.pid).getThread(event.tid);
            var subRowIndex = state.openSlices.length;
            thread.getSubrow(subRowIndex).push(slice);

            // Add the slice to the subSlices array of its parent.
            if (state.openSlices.length) {
              var parentSlice = state.openSlices[state.openSlices.length - 1];
              parentSlice.subSlices.push(slice);
            }
          }
        } else if (event.ph == 'I') {
          // TODO(nduca): Implement parsing of immediate events.
          console.log('Parsing of I-type events not implemented.');
        } else {
          throw new Error('Unrecognized event phase: ' + event.ph +
                          '(' + event.name + ')');
        }
      }
      this.updateBounds();

      this.shiftWorldToMicroseconds();

      var boost = (this.maxTimestamp - this.minTimestamp) * 0.15;
      this.minTimestamp = this.minTimestamp - boost;
      this.maxTimestamp = this.maxTimestamp + boost;
    },

    updateBounds: function() {
      var wmin = Infinity;
      var wmax = -wmin;
      var threads = this.getAllThreads();
      for (var tI = 0; tI < threads.length; tI++) {
        var thread = threads[tI];
        thread.updateBounds();
        if (thread.minTimestamp && thread.maxTimestamp) {
          wmin = Math.min(wmin, thread.minTimestamp);
          wmax = Math.max(wmax, thread.maxTimestamp);
        }
      }
      this.minTimestamp = wmin;
      this.maxTimestamp = wmax;
    },

    shiftWorldToMicroseconds: function() {
      var timeBase = this.minTimestamp;
      var threads = this.getAllThreads();
      for (var tI = 0; tI < threads.length; tI++) {
        var thread = threads[tI];
        var shiftSubRow = function(subRow) {
          for (var tS = 0; tS < subRow.length; tS++) {
            var slice = subRow[tS];
            slice.start = (slice.start - timeBase) / 1000;
            slice.duration /= 1000;
          }
        };
        for (var tSR = 0; tSR < thread.subRows.length; tSR++) {
          shiftSubRow(thread.subRows[tSR]);
        }
        for (var tSR = 0; tSR < thread.nonNestedSubRows.length; tSR++) {
          shiftSubRow(thread.nonNestedSubRows[tSR]);
        }
      }

      this.updateBounds();
    },

    getAllThreads: function() {
      var threads = [];
      for (var pid in this.processes) {
        var process = this.processes[pid];
        for (var tid in process.threads) {
          threads.push(process.threads[tid]);
        }
      }
      return threads;
    }

  };

  return {
    TimelineSlice: TimelineSlice,
    TimelineThread: TimelineThread,
    TimelineProcess: TimelineProcess,
    TimelineModel: TimelineModel
  };
});
