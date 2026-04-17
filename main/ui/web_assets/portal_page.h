/**
 * @file portal_page.h
 * @brief Web portal HTML/CSS/JS assets
 *
 * These assets are embedded as binary objects at link time.
 * To modify the UI, edit the .html files in ui/web_assets/ and rebuild.
 */

#pragma once

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

extern const char success_html_start[] asm("_binary_success_html_start");
extern const char success_html_end[] asm("_binary_success_html_end");

extern const char error_html_start[] asm("_binary_error_html_start");
extern const char error_html_end[] asm("_binary_error_html_end");
