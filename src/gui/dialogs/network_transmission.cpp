/*
   Copyright (C) 2011 - 2018 Sergey Popov <loonycyborg@gmail.com>
   Part of the Battle for Wesnoth Project https://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "gui/dialogs/network_transmission.hpp"

#include "gettext.hpp"
#include "gui/auxiliary/find_widget.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/progress_bar.hpp"
#include "gui/widgets/label.hpp"
#include "gui/widgets/settings.hpp"
#include "gui/widgets/window.hpp"
#include "log.hpp"
#include "serialization/string_utils.hpp"
#include "wesnothd_connection.hpp"

#include <chrono>

namespace gui2
{
namespace dialogs
{
using namespace std::chrono_literals;

REGISTER_DIALOG(network_transmission)

network_transmission::pump_monitor::pump_monitor(connection_data*& connection)
	: connection_(connection)
	, window_()
	, completed_(0)
	, total_(0)
	, poller_(std::async(std::launch::async, [this]() {
		while(true) {
			// connection_ will be null if we reset the ptr in the main thread before canceling the dialog
			if(!connection_) {
				return false;
			}

			// Check for updates
			connection_->poll();

			if(connection_->finished()) {
				return true;
			}

			completed_ = connection_->current();
			total_ = connection_->total();

			std::this_thread::sleep_for(10ms);
		}
	}))
{
}

void network_transmission::pump_monitor::process(events::pump_info&)
{
	if(!window_) {
		return;
	}

	// Check if the thread is complete. If it is, loading is done.
	if(poller_.wait_for(0ms) == std::future_status::ready && poller_.get()) {
		window_.get().set_retval(retval::OK);
		return;
	}

	if(total_) {
		find_widget<progress_bar>(&(window_.get()), "progress", false)
			.set_percentage((completed_ * 100.) / total_);

		std::ostringstream ss;
		ss
			<< utils::si_string(completed_, true, _("unit_byte^B")) << "/"
			<< utils::si_string(total_, true, _("unit_byte^B"));

		find_widget<label>(&(window_.get()), "numeric_progress", false)
			.set_label(ss.str());

		window_->invalidate_layout();
	}
}

network_transmission::network_transmission(
		connection_data& connection,
		const std::string& title,
		const std::string& subtitle)
	: connection_(&connection)
	, pump_monitor_(connection_)
	, subtitle_(subtitle)
{
	register_label("title", true, title, false);
	set_restore(true);
}

void network_transmission::pre_show(window& window)
{
	// ***** ***** ***** ***** Set up the widgets ***** ***** ***** *****
	if(!subtitle_.empty()) {
		label& subtitle_label = find_widget<label>(&window, "subtitle", false);

		subtitle_label.set_label(subtitle_);
		subtitle_label.set_use_markup(true);
	}

	pump_monitor_.window_ = window;
}

void network_transmission::post_show(window& /*window*/)
{
	pump_monitor_.window_.reset();

	if(get_retval() == retval::CANCEL) {
		// The worker thread holds a reference to connection_ and checks whether it's null before polling.
		// The actual object to which connection_ is pointing will be destroyed by cancel(), so we're
		// setting connection_ to null here before that so the worker thread will exit next run.
		connection_data* const connection_ptr_copy = connection_;
		connection_ = nullptr;

		// Wait for the current polling loop to conclude before exiting so we don't invalidate the pointer
		// mid-loop. The thread should return false if connection_ is null.
		pump_monitor_.poller_.wait();
		assert(pump_monitor_.poller_.get() == false);

		connection_ptr_copy->cancel();
	}
}

} // namespace dialogs
} // namespace gui2
