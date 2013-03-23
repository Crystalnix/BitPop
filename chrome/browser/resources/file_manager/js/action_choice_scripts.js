// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The include directives are put into Javascript-style comments to prevent
// parsing errors in non-flattened mode. The flattener still sees them.
// Note that this makes the flattener to comment out the first line of the
// included file but that's all right since any javascript file should start
// with a copyright comment anyway.

//<include src="../../shared/js/load_time_data.js"/>
//<include src="../../shared/js/util.js"/>
//<include src="../../shared/js/i18n_template_no_process.js"/>

//<include src="../../shared/js/cr.js"/>
//<include src="../../shared/js/event_tracker.js"/>
//<include src="../../shared/js/cr/ui.js"/>
//<include src="../../shared/js/cr/event_target.js"/>
//<include src="../../shared/js/cr/ui/touch_handler.js"/>

//<include src="util.js"/>
//<include src="file_type.js"/>
//<include src="path_util.js"/>
//<include src="volume_manager.js"/>
//<include src="metadata/metadata_cache.js"/>
//<include src="metrics.js"/>
//<include src="image_editor/image_util.js"/>
//<include src="media/media_util.js"/>

//<include src="action_choice.js"/>
