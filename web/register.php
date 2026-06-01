<?php
/**
 * register.php - Account registration for deMUSE
 *
 * Creates an unverified account and sends a verification email.
 * Player objects are NOT created here — that happens on first game login.
 */

require_once __DIR__ . '/db.php';
require_once __DIR__ . '/mailer.php';

start_session();

$error = '';
$success = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $username = trim($_POST['username'] ?? '');
    $password = $_POST['password'] ?? '';
    $confirm  = $_POST['confirm'] ?? '';
    $email    = trim($_POST['email'] ?? '');

    /* CSRF — refuse the POST if the hidden token does not match the
     * session. Without this, an attacker-hosted form could trigger
     * registrations from a victim's browser. */
    if (!csrf_check()) {
        $error = 'Your session expired. Please try again.';
    }

    /* Rate limit by IP. 5 attempts per hour is generous for legitimate
     * use but stops bulk-registration / username-enumeration sweeps. */
    if (!$error) {
        if (!rate_limit_check('register', client_ip(), 5, 3600)) {
            $error = 'Too many registration attempts. Please try again later.';
        }
    }

    /* Validate username */
    if (!$error) {
        if (strlen($username) < 3 || strlen($username) > 50) {
            $error = 'Username must be between 3 and 50 characters.';
        } elseif (!preg_match('/^[A-Za-z][A-Za-z0-9_-]*$/', $username)) {
            $error = 'Username must start with a letter and contain only letters, numbers, hyphens, and underscores.';
        } elseif (strpos($username, ':') !== false) {
            $error = 'Username cannot contain a colon.';
        }
    }

    /* Validate password */
    if (!$error) {
        if (strlen($password) < 8) {
            $error = 'Password must be at least 8 characters.';
        } elseif ($password !== $confirm) {
            $error = 'Passwords do not match.';
        }
    }

    /* Validate email */
    if (!$error) {
        if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
            $error = 'Please enter a valid email address.';
        }
    }

    /* Single combined uniqueness check. The previous two-message form
     * ("username taken" vs "email taken") allowed an attacker to
     * enumerate which field conflicted. */
    if (!$error) {
        $pdo = get_pdo();
        $stmt = $pdo->prepare(
            'SELECT account_id FROM accounts
             WHERE username = :u OR email = :e LIMIT 1'
        );
        $stmt->execute(['u' => $username, 'e' => $email]);
        if ($stmt->fetch()) {
            $error = 'Registration failed: that username or email is already in use.';
        }
    }

    /* Create the account. The UNIQUE indexes on username and email are the
     * authoritative uniqueness guard — the pre-check above is only for a
     * friendly message and can be raced by a concurrent registration. Catch
     * the duplicate-key error (SQLSTATE 23000) and map it to the same generic
     * message, so a lost race yields the intended response instead of an
     * uncaught exception (HTTP 500) and without revealing which field
     * collided. */
    if (!$error) {
        $hash = password_hash($password, PASSWORD_BCRYPT);

        try {
            $stmt = $pdo->prepare(
                'INSERT INTO accounts (username, password_hash, email, is_verified, is_disabled)
                 VALUES (:u, :h, :e, 0, 0)'
            );
            $stmt->execute(['u' => $username, 'h' => $hash, 'e' => $email]);
            $account_id = (int)$pdo->lastInsertId();
        } catch (PDOException $e) {
            if ($e->getCode() === '23000') {
                $error = 'Registration failed: that username or email is already in use.';
            } else {
                throw $e;
            }
        }
    }

    /* Generate the verification token and send the email — only once the
     * account row actually exists (i.e. the INSERT above did not collide). */
    if (!$error) {
        /* Generate email verification token (24-hour expiry) */
        $token = bin2hex(random_bytes(32));
        $stmt = $pdo->prepare(
            'INSERT INTO email_verify_tokens (account_id, token, expires_at)
             VALUES (:a, :t, DATE_ADD(NOW(), INTERVAL 24 HOUR))'
        );
        $stmt->execute(['a' => $account_id, 't' => $token]);

        /* Build the verification URL from the canonical web_url_base
         * config value. Never use $_SERVER['HTTP_HOST'] — it is
         * attacker-controlled and would let a registrant phish the
         * verification link to a host they control. */
        $base = get_web_url_base();
        if ($base === null) {
            error_log('deMUSE register: web_url_base config is unset; cannot send verification email');
            $success = 'Account created, but email verification is not configured on this server. '
                     . 'Please contact an administrator to verify your account.';
        } else {
            $verify_url = $base . '/verify.php?token=' . urlencode($token);

            $body = "Welcome to deMUSE!\n\n"
                  . "Please verify your account by clicking the link below:\n\n"
                  . "$verify_url\n\n"
                  . "This link expires in 24 hours.\n\n"
                  . "If you did not create this account, you can ignore this email.\n";

            $sent = send_email($email, 'deMUSE Account Verification', $body);

            if ($sent) {
                $success = 'Account created! Please check your email to verify your account.';
            } else {
                $success = 'Account created, but we could not send the verification email. '
                         . 'Please contact an administrator.';
            }
        }
    }
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>deMUSE - Register</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: #1a1a2e; color: #c0c0c0;
            display: flex; justify-content: center; align-items: center;
            min-height: 100vh;
        }
        .container {
            background: #16213e; padding: 2rem; border-radius: 8px;
            width: 100%; max-width: 400px; box-shadow: 0 4px 20px rgba(0,0,0,0.5);
        }
        h1 { color: #e0e0e0; margin-bottom: 1.5rem; text-align: center; }
        label { display: block; margin-bottom: 0.3rem; color: #a0a0a0; font-size: 0.9rem; }
        input[type="text"], input[type="password"], input[type="email"] {
            width: 100%; padding: 0.6rem; margin-bottom: 1rem;
            background: #0f3460; border: 1px solid #333; border-radius: 4px;
            color: #e0e0e0; font-size: 1rem;
        }
        input:focus { outline: none; border-color: #00a8cc; }
        button {
            width: 100%; padding: 0.7rem; background: #00a8cc; color: #fff;
            border: none; border-radius: 4px; font-size: 1rem; cursor: pointer;
        }
        button:hover { background: #0090b0; }
        .error { color: #ff6b6b; margin-bottom: 1rem; font-size: 0.9rem; }
        .success { color: #51cf66; margin-bottom: 1rem; font-size: 0.9rem; }
        .links { margin-top: 1rem; text-align: center; font-size: 0.9rem; }
        .links a { color: #00a8cc; text-decoration: none; }
        .links a:hover { text-decoration: underline; }
    </style>
</head>
<body>
<div class="container">
    <h1>Create Account</h1>
    <?php if ($error): ?>
        <div class="error"><?= htmlspecialchars($error) ?></div>
    <?php endif; ?>
    <?php if ($success): ?>
        <div class="success"><?= htmlspecialchars($success) ?></div>
        <div class="links"><a href="login.php">Go to Login</a></div>
    <?php else: ?>
        <form method="post">
            <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(csrf_token()) ?>">
            <label for="username">Username</label>
            <input type="text" id="username" name="username" required
                   value="<?= htmlspecialchars($username ?? '') ?>"
                   minlength="3" maxlength="50" autocomplete="username">

            <label for="email">Email</label>
            <input type="email" id="email" name="email" required
                   value="<?= htmlspecialchars($email ?? '') ?>"
                   autocomplete="email">

            <label for="password">Password</label>
            <input type="password" id="password" name="password" required
                   minlength="8" autocomplete="new-password">

            <label for="confirm">Confirm Password</label>
            <input type="password" id="confirm" name="confirm" required
                   minlength="8" autocomplete="new-password">

            <button type="submit">Register</button>
        </form>
        <div class="links"><a href="login.php">Already have an account? Log in</a></div>
    <?php endif; ?>
</div>
</body>
</html>
