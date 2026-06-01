-- defaults.sql - Default configuration values for deMUSE
--
-- This file is the authoritative source of all config default values.
-- It replaces the old config/config.c hardcoded defaults.
--
-- Usage:
--   mysql -u demuse -p demuse < config/defaults.sql
--
-- Or via the installer:
--   bash config/install.sh
--
-- IDEMPOTENT / SAFE TO RE-RUN:
-- All inserts use INSERT IGNORE, which silently skips a row whose
-- (unique) key is already present. That means:
--   * Fresh install — every default lands.
--   * Upgrade run — only newly added defaults land; values an operator
--     has tuned via @config or `UPDATE config ...` are preserved.
--   * To wipe a tuned value back to its default, DELETE the row first
--     (or run an explicit UPDATE) — re-running this file alone will
--     not roll a customization back.
--
-- The default channel seeds at the bottom follow the same pattern.

USE demuse;

-- ============================================================================
-- STRING CONFIG VALUES
-- ============================================================================

INSERT IGNORE INTO config (config_key, config_value, config_type) VALUES
('muse_name', 'YourMUSE', 'STR'),
('start_quota', '100', 'STR'),
('guest_prefix', 'Guest', 'STR'),
('guest_alias_prefix', 'G', 'STR'),
('guest_description', 'You see a guest.', 'STR'),
('bad_object_doomsday', '600', 'STR'),
('default_doomsday', '600', 'STR'),
('def_db_in', 'db/mdb', 'STR'),
('def_db_out', 'db/mdb', 'STR'),
('stdout_logfile', 'logs/out.log', 'STR'),
('wd_logfile', 'logs/wd.log', 'STR'),
('muse_pid_file', 'logs/muse_pid', 'STR'),
('wd_pid_file', 'logs/wd_pid', 'STR'),
('create_msg', '', 'STR'),
('motd_msg', '-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-\\nPlease edit motd_msg via @config.\\n-=-=-=-=--=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-', 'STR'),
('motd_msg_player', '1', 'REF'),
('welcome_msg', '\\n                        Welcome to deMUSE\\n\\n     Please log in via the web interface to receive a connection token.\\n     To connect as a guest, type: connect guest\\n     To see who is online, type: WHO\\n\\n     If you have a token, type: connect token:<your-token>', 'STR'),
('guest_msg', 'You have connected as a guest, please be courteous.\\nReport any abuse you receive, in detail, to an administrator.\\n\\nSee \"help register\" for details on getting a character.\\nType \"who\" to see who''s online.\\nAnd try using \"help commands\" to find your way around.\\nHave a nice day.  Page an unidle guide or director for assistance.', 'STR'),
('register_msg', 'Try: connect Guest, or email an administrator', 'STR'),
('leave_msg', 'Thanks for visiting!', 'STR'),
('guest_lockout_msg', 'Guest connections are not available at this time.', 'STR'),
('welcome_lockout_msg', 'Your connection has been refused.', 'STR'),
('maintenance_msg', 'This server is currently in maintenance mode. Only staff may connect at this time.', 'STR'),
('flushed_message', '<Output Flushed>\n', 'STR'),
('online_message', 'online.\n', 'STR'),
('reboot_message', 'reloading, please hold.\n', 'STR'),
('shutdown_message', 'says ''This is your captain speaking. Light em up, cuz we''re going down''\n', 'STR'),
('lockout_message', 'is currently under restricted access conditions.\nPlease try again later.\n', 'STR'),
('first_login', 'First login: It always hurts the first time.', 'STR'),
('loginstats_file', 'db/loginstatsdb', 'STR'),
('smtp_server', 'smtp.gmail.com', 'STR'),
('smtp_username', 'your-game@gmail.com', 'STR'),
('smtp_password', 'your-app-password', 'STR'),
('smtp_from', 'noreply@yourmud.com', 'STR'),
-- Comma-separated list of reverse-proxy IPs whose X-Forwarded-For header
-- the WebSocket layer will trust. Connections from any other peer have
-- their X-Forwarded-For ignored and the direct peer IP is used instead.
-- Matching is an EXACT textual comparison against the address libwebsockets
-- reports for the peer; IPv6 forms are NOT normalized, so list the proxy in
-- the same form lws renders it (e.g. '::1', not '0:0:0:0:0:0:0:1', and an
-- IPv4-mapped proxy as '::ffff:127.0.0.1'). A mismatch fails closed (XFF
-- ignored) — safe, but it silently disables passthrough for that proxy.
('trusted_proxies', '127.0.0.1,::1', 'STR'),
-- Canonical base URL used in verification and password-reset emails, e.g.
-- 'https://demuse.example.com'. MUST be set for account verification and
-- password reset to work — when empty, those emails are silently NOT sent
-- (forgot.php logs and skips). Never use the request Host header — it is
-- attacker-controlled (host-header injection in outgoing email links).
('web_url_base', '', 'STR');

-- ============================================================================
-- NUMERIC (int) CONFIG VALUES
-- ============================================================================

INSERT IGNORE INTO config (config_key, config_value, config_type) VALUES
('allow_create', '0', 'NUM'),
('initial_credits', '2000', 'NUM'),
('allowance', '250', 'NUM'),
('number_guests', '30', 'NUM'),
('announce_guests', '0', 'NUM'),
('announce_connects', '0', 'NUM'),
('inet_port', '4208', 'NUM'),
('websocket_port', '4209', 'NUM'),
('fixup_interval', '1243', 'NUM'),
('dump_interval', '2714', 'NUM'),
('garbage_chunk', '3', 'NUM'),
('max_output', '32767', 'NUM'),
('max_output_pueblo', '65535', 'NUM'),
('max_input', '1024', 'NUM'),
('command_time_msec', '1000', 'NUM'),
('command_burst_size', '100', 'NUM'),
('commands_per_time', '1', 'NUM'),
('warning_chunk', '50', 'NUM'),
('warning_bonus', '30', 'NUM'),
('guest_enabled', '1', 'NUM'),
('maintenance_level', '0', 'NUM'),
('thing_cost', '50', 'NUM'),
('exit_cost', '1', 'NUM'),
('room_cost', '100', 'NUM'),
('robot_cost', '1000', 'NUM'),
('channel_cost', '100', 'NUM'),
('univ_cost', '100', 'NUM'),
('link_cost', '1', 'NUM'),
('find_cost', '10', 'NUM'),
('search_cost', '10', 'NUM'),
('page_cost', '1', 'NUM'),
('announce_cost', '50', 'NUM'),
('queue_cost', '100', 'NUM'),
('queue_loss', '150', 'NUM'),
('max_queue', '1000', 'NUM'),
('channel_name_limit', '32', 'NUM'),
('player_name_limit', '32', 'NUM'),
('player_reference_limit', '5', 'NUM'),
('min_idle', '1200', 'NUM'),
('max_idle', '3600', 'NUM'),
('num_welcome_messages', '10', 'NUM'),
('loginstats_max_backups', '3', 'NUM'),
('max_emails_per_day', '10', 'NUM'),
('email_cooldown', '60', 'NUM'),
('max_email_length', '4096', 'NUM'),
('smtp_port', '465', 'NUM'),
('smtp_use_ssl', '1', 'NUM');

-- ============================================================================
-- DBREF (long) CONFIG VALUES
-- ============================================================================

INSERT IGNORE INTO config (config_key, config_value, config_type) VALUES
('player_start', '30', 'REF'),
('guest_start', '25', 'REF'),
('default_room', '0', 'REF'),
('root', '1', 'REF');

-- ============================================================================
-- LONG CONFIG VALUES
-- ============================================================================

INSERT IGNORE INTO config (config_key, config_value, config_type) VALUES
('default_idletime', '300', 'LNG'),
('guest_boot_time', '300', 'LNG'),
('max_pennies', '1000000', 'LNG');

-- ============================================================================
-- PERMISSION DENIED MESSAGES (array: perm_messages-N)
-- ============================================================================

INSERT IGNORE INTO config (config_key, config_value, config_type) VALUES
('perm_messages-1', 'Permission denied.', 'STR'),
('perm_messages-2', 'Ummm... no.', 'STR'),
('perm_messages-3', 'Lemme think about that.. No.', 'STR');

-- ============================================================================
-- HELP TOPICS AND NEWS - See config/help_seed.sql
-- ============================================================================
-- Default help topics and a welcome news article are in a separate file
-- due to size. Load it after this file:
--   mysql -u demuse -p demuse < config/help_seed.sql

-- ============================================================================
-- DEFAULT SYSTEM CHANNELS
-- ============================================================================
-- These are seeded once. Existing channels are not overwritten (INSERT IGNORE).
-- Owner is set to dbref 1 (root). Prefix chars (*._) are NOT stored in the
-- name; min_level controls access and prefix is prepended at display time.
-- The SEE_OK flag is 0x80 (128).

INSERT IGNORE INTO channels (name, cname, owner, flags, is_system, min_level) VALUES
('log_imp',     'log_imp',     1, 128, 1, 0),
('log_sens',    'log_sens',    1, 128, 1, 3),
('log_err',     'log_err',     1, 128, 1, 0),
('log_io',      'log_io',      1, 128, 1, 3),
('log_gripe',   'log_gripe',   1, 128, 1, 0),
('log_force',   'log_force',   1, 128, 1, 3),
('log_prayer',  'log_prayer',  1, 128, 1, 0),
('log_combat',  'log_combat',  1, 128, 1, 0),
('log_suspect', 'log_suspect', 1, 128, 1, 3),
('dbinfo',      'dbinfo',      1, 128, 1, 0),
('dc',          'dc',          1, 128, 1, 3),
('pub_io',      'pub_io',      1, 128, 1, 0),
('connect',     'connect',     1, 128, 1, 0);
