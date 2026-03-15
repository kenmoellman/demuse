<?php
/**
 * mailer.php - Email sending helper for deMUSE web interface
 *
 * Reads SMTP configuration from the MariaDB config table (same smtp_*
 * variables used by the C email system in comm/email.c).
 */

require_once __DIR__ . '/db.php';

/**
 * Send an email using SMTP settings from the MariaDB config table.
 *
 * @param string $to      Recipient email address
 * @param string $subject Email subject
 * @param string $body    Plain text email body
 * @return bool True on success, false on failure
 */
function send_email(string $to, string $subject, string $body): bool
{
    $from = get_config('smtp_from');
    if (!$from) {
        error_log('deMUSE mailer: smtp_from not configured');
        return false;
    }

    $smtp_server   = get_config('smtp_server');
    $smtp_port     = (int)(get_config('smtp_port') ?: 25);
    $smtp_use_ssl  = (int)(get_config('smtp_use_ssl') ?: 0);
    $smtp_username = get_config('smtp_username');
    $smtp_password = get_config('smtp_password');

    /* If no SMTP server configured, fall back to PHP mail() */
    if (!$smtp_server) {
        $headers = "From: $from\r\n";
        $headers .= "Content-Type: text/plain; charset=UTF-8\r\n";
        $headers .= "X-Mailer: deMUSE-Web/1.0\r\n";
        return mail($to, $subject, $body, $headers);
    }

    /* Use fsockopen for direct SMTP delivery */
    $prefix = $smtp_use_ssl ? 'ssl://' : '';
    $fp = @fsockopen($prefix . $smtp_server, $smtp_port, $errno, $errstr, 10);
    if (!$fp) {
        error_log("deMUSE mailer: cannot connect to $smtp_server:$smtp_port - $errstr");
        return false;
    }

    /* Helper to read SMTP response */
    $read_response = function() use ($fp): string {
        $response = '';
        while ($line = fgets($fp, 512)) {
            $response .= $line;
            if (isset($line[3]) && $line[3] === ' ') break;
        }
        return $response;
    };

    /* Helper to send command and check response code */
    $send_cmd = function(string $cmd, int $expect) use ($fp, $read_response): bool {
        fwrite($fp, $cmd . "\r\n");
        $resp = $read_response();
        return (int)substr($resp, 0, 3) === $expect;
    };

    $ok = true;

    /* Read greeting */
    $read_response();

    /* EHLO */
    $ok = $ok && $send_cmd('EHLO demuse', 250);

    /* AUTH if credentials provided */
    if ($ok && $smtp_username && $smtp_password) {
        $ok = $ok && $send_cmd('AUTH LOGIN', 334);
        $ok = $ok && $send_cmd(base64_encode($smtp_username), 334);
        $ok = $ok && $send_cmd(base64_encode($smtp_password), 235);
    }

    /* MAIL FROM, RCPT TO, DATA */
    $ok = $ok && $send_cmd("MAIL FROM:<$from>", 250);
    $ok = $ok && $send_cmd("RCPT TO:<$to>", 250);
    $ok = $ok && $send_cmd('DATA', 354);

    if ($ok) {
        $date = date('r');
        $msg  = "Date: $date\r\n";
        $msg .= "From: $from\r\n";
        $msg .= "To: $to\r\n";
        $msg .= "Subject: $subject\r\n";
        $msg .= "Content-Type: text/plain; charset=UTF-8\r\n";
        $msg .= "X-Mailer: deMUSE-Web/1.0\r\n";
        $msg .= "\r\n";
        $msg .= str_replace("\n.", "\n..", $body);
        $msg .= "\r\n";

        fwrite($fp, $msg);
        $ok = $send_cmd('.', 250);
    }

    $send_cmd('QUIT', 221);
    fclose($fp);

    if (!$ok) {
        error_log("deMUSE mailer: SMTP transaction failed sending to $to");
    }

    return $ok;
}
