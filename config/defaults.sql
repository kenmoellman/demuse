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
-- Uses INSERT ... ON DUPLICATE KEY UPDATE so it's safe to run multiple times.
-- Existing customized values will be overwritten!

USE demuse;

-- ============================================================================
-- STRING CONFIG VALUES
-- ============================================================================

INSERT INTO config (config_key, config_value, config_type) VALUES
('muse_name', 'YourMUSE', 'STR'),
('chan_dbinfo', 'dbinfo', 'STR'),
('chan_dc', 'dc', 'STR'),
('chan_pubio', 'pub_io', 'STR'),
('chan_connect', 'connect', 'STR'),
('chan_warn_prefix', 'warn_', 'STR'),
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
('create_msg_file', 'msgs/create.txt', 'STR'),
('motd_msg_file', 'msgs/motd.txt', 'STR'),
('welcome_msg_file', 'msgs/welcome.txt', 'STR'),
('guest_msg_file', 'msgs/guest.txt', 'STR'),
('register_msg_file', 'msgs/register.txt', 'STR'),
('leave_msg_file', 'msgs/leave.txt', 'STR'),
('guest_lockout_file', '../config/guest-lockout', 'STR'),
('welcome_lockout_file', '../config/welcome-lockout', 'STR')
ON DUPLICATE KEY UPDATE config_value=VALUES(config_value), config_type=VALUES(config_type);

-- ============================================================================
-- NUMERIC (int) CONFIG VALUES
-- ============================================================================

INSERT INTO config (config_key, config_value, config_type) VALUES
('allow_create', '0', 'NUM'),
('initial_credits', '2000', 'NUM'),
('allowance', '250', 'NUM'),
('number_guests', '30', 'NUM'),
('announce_guests', '0', 'NUM'),
('announce_connects', '0', 'NUM'),
('inet_port', '4208', 'NUM'),
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
('enable_lockout', '1', 'NUM'),
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
('player_reference_limit', '5', 'NUM')
ON DUPLICATE KEY UPDATE config_value=VALUES(config_value), config_type=VALUES(config_type);

-- ============================================================================
-- DBREF (long) CONFIG VALUES
-- ============================================================================

INSERT INTO config (config_key, config_value, config_type) VALUES
('player_start', '30', 'REF'),
('guest_start', '25', 'REF'),
('default_room', '0', 'REF'),
('root', '1', 'REF')
ON DUPLICATE KEY UPDATE config_value=VALUES(config_value), config_type=VALUES(config_type);

-- ============================================================================
-- LONG CONFIG VALUES
-- ============================================================================

INSERT INTO config (config_key, config_value, config_type) VALUES
('default_idletime', '300', 'LNG'),
('guest_boot_time', '300', 'LNG'),
('max_pennies', '1000000', 'LNG')
ON DUPLICATE KEY UPDATE config_value=VALUES(config_value), config_type=VALUES(config_type);

-- ============================================================================
-- PERMISSION DENIED MESSAGES (array: perm_messages-N)
-- ============================================================================

INSERT INTO config (config_key, config_value, config_type) VALUES
('perm_messages-1', 'Permission denied.', 'STR'),
('perm_messages-2', 'Ummm... no.', 'STR'),
('perm_messages-3', 'Lemme think about that.. No.', 'STR')
ON DUPLICATE KEY UPDATE config_value=VALUES(config_value), config_type=VALUES(config_type);

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
