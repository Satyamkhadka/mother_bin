/**
 * @file portal_page.h
 * @brief Web portal HTML/CSS/JS assets
 * 
 * These are embedded strings that are served to the client.
 * The JavaScript fetches /config to dynamically generate the form.
 */

#pragma once

/* ============== Main Settings Page ==============
 * This is a modern, responsive web UI that:
 * 1. Fetches /config to get the configuration schema
 * 2. Dynamically generates form fields based on schema
 * 3. Handles form submission via POST /settings
 */

#define PORTAL_PAGE_HTML \
"<!DOCTYPE html>" \
"<html lang=\"en\">" \
"<head>" \
"<meta charset=\"UTF-8\">" \
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">" \
"<title>Device Configuration</title>" \
"<style>" \
"* { box-sizing: border-box; margin: 0; padding: 0; }" \
"body { " \
"  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;" \
"  background: #f5f5f7;" \
"  color: #1d1d1f;" \
"  line-height: 1.5;" \
"  padding: 20px;" \
"  min-height: 100vh;" \
"}" \
".container { max-width: 480px; margin: 0 auto; }" \
".card { " \
"  background: white;" \
"  border-radius: 16px;" \
"  padding: 24px;" \
"  margin-bottom: 16px;" \
"  box-shadow: 0 2px 12px rgba(0,0,0,0.08);" \
"}" \
".header { text-align: center; margin-bottom: 24px; }" \
".header h1 { font-size: 24px; font-weight: 600; margin-bottom: 8px; }" \
".header p { color: #86868b; font-size: 14px; }" \
".section-title { " \
"  font-size: 14px; font-weight: 600; text-transform: uppercase;" \
"  color: #86868b; margin-bottom: 16px; letter-spacing: 0.5px;" \
"}" \
".form-group { margin-bottom: 20px; }" \
"label { " \
"  display: block; font-size: 14px; font-weight: 500;" \
"  margin-bottom: 8px; color: #1d1d1f;" \
"}" \
"input[type=\"text\"], input[type=\"password\"], input[type=\"number\"], select { " \
"  width: 100%; padding: 12px 16px; border: 1px solid #d2d2d7;" \
"  border-radius: 10px; font-size: 16px; background: #fbfbfd;" \
"  transition: border-color 0.2s, box-shadow 0.2s;" \
"}" \
"input:focus, select:focus { " \
"  outline: none; border-color: #0071e3;" \
"  box-shadow: 0 0 0 4px rgba(0,113,227,0.15);" \
"}" \
".toggle { position: relative; display: inline-block; width: 50px; height: 30px; }" \
".toggle input { opacity: 0; width: 0; height: 0; }" \
".slider { " \
"  position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;" \
"  background-color: #d2d2d7; transition: .3s; border-radius: 30px;" \
"}" \
".slider:before { " \
"  position: absolute; content: \"\"; height: 22px; width: 22px;" \
"  left: 4px; bottom: 4px; background-color: white;" \
"  transition: .3s; border-radius: 50%;" \
"}" \
"input:checked + .slider { background-color: #34c759; }" \
"input:checked + .slider:before { transform: translateX(20px); }" \
".btn { " \
"  width: 100%; padding: 16px; border: none; border-radius: 12px;" \
"  font-size: 16px; font-weight: 600; cursor: pointer;" \
"  transition: all 0.2s;" \
"}" \
".btn-primary { " \
"  background: #0071e3; color: white;" \
"}" \
".btn-primary:hover { background: #0077ed; }" \
".btn-primary:active { transform: scale(0.98); }" \
".btn-success { background: #34c759; color: white; }" \
".btn:disabled { opacity: 0.6; cursor: not-allowed; }" \
".network-list { max-height: 200px; overflow-y: auto; }" \
".network-item { " \
"  display: flex; align-items: center; padding: 12px;" \
"  border-radius: 10px; cursor: pointer; transition: background 0.2s;" \
"  margin-bottom: 4px;" \
"}" \
".network-item:hover { background: #f5f5f7; }" \
".network-item.selected { background: #0071e3; color: white; }" \
".network-item.selected .network-secure { color: rgba(255,255,255,0.8); }" \
".network-icon { width: 24px; height: 24px; margin-right: 12px; }" \
".network-info { flex: 1; }" \
".network-name { font-weight: 500; }" \
".network-secure { font-size: 12px; color: #86868b; }" \
".hidden-form { display: none; }" \
".message { " \
"  padding: 16px; border-radius: 10px; margin-bottom: 16px;" \
"  text-align: center; font-size: 14px;" \
"}" \
".message.success { background: #d4edda; color: #155724; }" \
".message.error { background: #f8d7da; color: #721c24; }" \
".loading { text-align: center; padding: 40px; color: #86868b; }" \
".spinner { " \
"  display: inline-block; width: 24px; height: 24px;" \
"  border: 3px solid #f3f3f3; border-top: 3px solid #0071e3;" \
"  border-radius: 50%; animation: spin 1s linear infinite;" \
"  margin-right: 8px;" \
"}" \
"@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }" \
"</style>" \
"</head>" \
"<body>" \
"<div class=\"container\">" \
"<div class=\"card\">" \
"<div class=\"header\">" \
"<h1>⚙️ Device Setup</h1>" \
"<p>Configure your device to connect to your network</p>" \
"</div>" \
"<div id=\"message\"></div>" \
"<form id=\"configForm\" method=\"POST\" action=\"/settings\">" \
"<!-- WiFi Section -->" \
"<div class=\"section-title\">WiFi Network</div>" \
"<div id=\"wifiSection\">" \
"<div class=\"loading\"><div class=\"spinner\"></div>Scanning...</div>" \
"</div>" \
"<div class=\"form-group hidden-form\" id=\"passwordGroup\">" \
"<label for=\"password\">Password</label>" \
"<input type=\"password\" id=\"password\" name=\"password\" placeholder=\"Enter WiFi password\">" \
"</div>" \
"<div class=\"form-group hidden-form\" id=\"hiddenSsidGroup\">" \
"<label for=\"hidden_ssid\">Hidden Network SSID</label>" \
"<input type=\"text\" id=\"hidden_ssid\" name=\"hidden_ssid\" placeholder=\"Enter network name\">" \
"</div>" \
"<div class=\"form-group\">" \
"<label>" \
"<input type=\"checkbox\" id=\"hiddenNetwork\" onchange=\"toggleHiddenNetwork()\">" \
" Connect to hidden network" \
"</label>" \
"</div>" \
"<!-- Dynamic Configuration Fields -->" \
"<div class=\"section-title\">Device Configuration</div>" \
"<div id=\"configFields\">" \
"<div class=\"loading\"><div class=\"spinner\"></div>Loading...</div>" \
"</div>" \
"<!-- Submit -->" \
"<button type=\"submit\" class=\"btn btn-primary\" id=\"submitBtn\">Save & Connect</button>" \
"</form>" \
"</div>" \
"</div>" \
"<script>" \
"let selectedSsid = '';" \
"let configSchema = null;" \
"/* Fetch configuration schema */" \
"fetch('/config')" \
".then(r => {" \
"if (!r.ok) throw new Error('HTTP ' + r.status);" \
"return r.json();" \
"})" \
".then(data => {" \
"configSchema = data;" \
"if (data.fields) renderConfigFields(data.fields);" \
"else throw new Error('Invalid response');" \
"})" \
".catch(e => {" \
"document.getElementById('configFields').innerHTML = '<div class=\\'message error\\'>Failed to load config: ' + escapeHtml(e.message) + '</div>';" \
"console.error('Config fetch error:', e);" \
"});" \
"/* Fetch WiFi networks*/" \
"fetch('/scan')" \
".then(r => {" \
"if (!r.ok) throw new Error('HTTP ' + r.status);" \
"return r.json();" \
"})" \
".then(data => {" \
"if (data.networks) renderNetworks(data.networks);" \
"else throw new Error('Invalid response');" \
"})" \
".catch(e => {" \
"document.getElementById('wifiSection').innerHTML = '<div class=\\'message error\\'>Scan failed: ' + escapeHtml(e.message) + '</div>';" \
"console.error('Scan fetch error:', e);" \
"});" \
"function renderNetworks(networks) {" \
"const container = document.getElementById('wifiSection');" \
"if (!networks || networks.length === 0) {" \
"container.innerHTML = '<div class=\\'message error\\'>No networks found</div>';" \
"return;" \
"}" \
"let html = '<div class=\\'network-list\\'>';" \
"networks.forEach(n => {" \
"const secure = n.auth !== 0;" \
"html += '<div class=\\'network-item\\' onclick=\\selectNetwork(\\'' + escapeHtml(n.ssid) + '\\', ' + secure + ')\\'>';" \
"html += '<div class=\\'network-icon\\'>' + (secure ? '🔒' : '📡') + '</div>';" \
"html += '<div class=\\'network-info\\'><div class=\\'network-name\\'>' + escapeHtml(n.ssid) + '</div>';" \
"html += '<div class=\\'network-secure\\'>' + (secure ? 'Secure' : 'Open') + ' • ' + n.rssi + 'dBm</div></div>';" \
"html += '</div>';" \
"});" \
"html += '</div>';" \
"container.innerHTML = html;" \
"}" \
"function selectNetwork(ssid, secure) {" \
"selectedSsid = ssid;" \
"document.querySelectorAll('.network-item').forEach(el => el.classList.remove('selected'));" \
"event.currentTarget.classList.add('selected');" \
"document.getElementById('hidden_ssid').value = ssid;" \
"document.getElementById('passwordGroup').classList.toggle('hidden-form', !secure);" \
"if (!secure) document.getElementById('password').value = '';" \
"}" \
"function toggleHiddenNetwork() {" \
"const isHidden = document.getElementById('hiddenNetwork').checked;" \
"document.getElementById('wifiSection').classList.toggle('hidden-form', isHidden);" \
"document.getElementById('hiddenSsidGroup').classList.toggle('hidden-form', !isHidden);" \
"if (isHidden) document.getElementById('passwordGroup').classList.remove('hidden-form');" \
"}" \
"function renderConfigFields(fields) {" \
"const container = document.getElementById('configFields');" \
"let html = '';" \
"fields.forEach(f => {" \
"html += '<div class=\\'form-group\\'>';" \
"html += '<label for=\\'' + f.name + '\\'>' + escapeHtml(f.label) + '</label>';" \
"if (f.type === 'string') {" \
"html += '<input type=\\'text\\' id=\\'' + f.name + '\\' name=\\'' + f.name + '\\' value=\\'' + escapeHtml(f.value || '') + '\\'>';" \
"} else if (f.type === 'number') {" \
"html += '<input type=\\'number\\' id=\\'' + f.name + '\\' name=\\'' + f.name + '\\' value=\\'' + f.value + '\\'' +" \
"' min=\\'' + (f.min || 0) + '\\' max=\\'' + (f.max || 999999) + '\\'>';" \
"} else if (f.type === 'boolean') {" \
"html += '<label class=\\'toggle\\'><input type=\\'checkbox\\' id=\\'' + f.name + '\\' name=\\'' + f.name + '\\'' + (f.value ? ' checked' : '') + '>';" \
"html += '<span class=\\'slider\\'></span></label>';" \
"}" \
"html += '</div>';" \
"});" \
"container.innerHTML = html;" \
"/* Convert checkboxes to hidden inputs on submit*/" \
"document.getElementById('configForm').addEventListener('submit', function(e) {" \
"fields.forEach(f => {" \
"if (f.type === 'boolean') {" \
"const cb = document.getElementById(f.name);" \
"if (cb) {" \
"cb.type = 'hidden';" \
"cb.value = cb.checked ? 'true' : 'false';" \
"}" \
"}" \
"});" \
"document.getElementById('submitBtn').disabled = true;" \
"document.getElementById('submitBtn').textContent = 'Connecting...';" \
"});" \
"}" \
"function escapeHtml(text) {" \
"const div = document.createElement('div');" \
"div.textContent = text;" \
"return div.innerHTML;" \
"}" \
"</script>" \
"</body></html>"

/* ============== Success Page ==============
 * Shown after successful configuration
 */
#define PORTAL_SUCCESS_HTML \
"<!DOCTYPE html>" \
"<html><head>" \
"<meta charset=\"UTF-8\">" \
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">" \
"<title>Configuration Saved</title>" \
"<style>" \
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;" \
"background: #f5f5f7; display: flex; align-items: center; justify-content: center;" \
"min-height: 100vh; margin: 0; }" \
".card { background: white; border-radius: 20px; padding: 48px; text-align: center;" \
"box-shadow: 0 4px 20px rgba(0,0,0,0.1); max-width: 400px; }" \
".icon { font-size: 64px; margin-bottom: 24px; }" \
"h1 { color: #1d1d1f; margin: 0 0 16px; }" \
"p { color: #86868b; margin: 0 0 32px; line-height: 1.5; }" \
".spinner { display: inline-block; width: 24px; height: 24px;" \
"border: 3px solid #34c759; border-top-color: transparent;" \
"border-radius: 50%; animation: spin 1s linear infinite; margin-right: 8px; }" \
"@keyframes spin { to { transform: rotate(360deg); } }" \
"</style></head><body>" \
"<div class=\"card\">" \
"<div class=\"icon\">✅</div>" \
"<h1>Configuration Saved!</h1>" \
"<p>Your device is now connecting to the WiFi network.<br>This may take a moment.</p>" \
"<div style=\"color: #34c759;\"><div class=\"spinner\"></div>Connecting...</div>" \
"</div></body></html>"

/* ============== Error Page ==============
 * Shown on errors
 */
#define PORTAL_ERROR_HTML \
"<!DOCTYPE html>" \
"<html><head>" \
"<meta charset=\"UTF-8\">" \
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">" \
"<title>Error</title>" \
"<style>" \
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;" \
"background: #f5f5f7; display: flex; align-items: center; justify-content: center;" \
"min-height: 100vh; margin: 0; }" \
".card { background: white; border-radius: 20px; padding: 48px; text-align: center;" \
"box-shadow: 0 4px 20px rgba(0,0,0,0.1); max-width: 400px; }" \
".icon { font-size: 64px; margin-bottom: 24px; }" \
"h1 { color: #1d1d1f; margin: 0 0 16px; }" \
"p { color: #86868b; margin: 0 0 32px; }" \
"a { color: #0071e3; text-decoration: none; font-weight: 500; }" \
"</style></head><body>" \
"<div class=\"card\">" \
"<div class=\"icon\">❌</div>" \
"<h1>Configuration Failed</h1>" \
"<p>There was an error saving your configuration.<br>Please try again.</p>" \
"<a href=\"/\">← Back to Settings</a>" \
"</div></body></html>"
