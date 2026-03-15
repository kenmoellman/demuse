<?php
/**
 * forgot.php - Password reset request for deMUSE
 *
 * Takes an email address, generates a reset token (1-hour expiry),
 * and sends a reset link. Always shows the same success message
 * regardless of whether the email exists (prevents enumeration).
 */

require_once __DIR__ . '/db.php';
require_once __DIR__ . '/mailer.php';

$message = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $email = trim($_POST['email'] ?? '');

    if (!$email || !filter_var($email, FILTER_VALIDATE_EMAIL)) {
        $message = 'Please enter a valid email address.';
    } else {
        $pdo = get_pdo();

        /* Look up account by email */
        $stmt = $pdo->prepare(
            'SELECT account_id, username FROM accounts
             WHERE email = :e AND is_verified = 1 AND is_disabled = 0'
        );
        $stmt->execute(['e' => $email]);
        $account = $stmt->fetch();

        if ($account) {
            /* Delete any existing reset tokens for this account */
            $stmt = $pdo->prepare(
                'DELETE FROM password_reset_tokens WHERE account_id = :a'
            );
            $stmt->execute(['a' => $account['account_id']]);

            /* Generate reset token (1-hour expiry) */
            $token = bin2hex(random_bytes(32));
            $stmt = $pdo->prepare(
                'INSERT INTO password_reset_tokens (account_id, token, expires_at)
                 VALUES (:a, :t, DATE_ADD(NOW(), INTERVAL 1 HOUR))'
            );
            $stmt->execute([
                'a' => $account['account_id'],
                't' => $token,
            ]);

            /* Send reset email */
            $reset_url = (isset($_SERVER['HTTPS']) ? 'https' : 'http')
                       . '://' . ($_SERVER['HTTP_HOST'] ?? 'localhost')
                       . '/reset.php?token=' . urlencode($token);

            $body = "Hello " . $account['username'] . ",\n\n"
                  . "A password reset was requested for your deMUSE account.\n\n"
                  . "Click the link below to set a new password:\n\n"
                  . "$reset_url\n\n"
                  . "This link expires in 1 hour.\n\n"
                  . "If you did not request this, you can ignore this email.\n";

            send_email($email, 'deMUSE Password Reset', $body);
        }

        /* Always show the same message (prevents email enumeration) */
        $message = 'If an account with that email exists, a reset link has been sent.';
    }
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>deMUSE - Forgot Password</title>
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
        input[type="email"] {
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
        .message { color: #51cf66; margin-bottom: 1rem; font-size: 0.9rem; }
        .links { margin-top: 1rem; text-align: center; font-size: 0.9rem; }
        .links a { color: #00a8cc; text-decoration: none; }
        .links a:hover { text-decoration: underline; }
    </style>
</head>
<body>
<div class="container">
    <h1>Reset Password</h1>
    <?php if ($message): ?>
        <div class="message"><?= htmlspecialchars($message) ?></div>
        <div class="links"><a href="login.php">Back to login</a></div>
    <?php else: ?>
        <p style="margin-bottom: 1rem; font-size: 0.9rem;">
            Enter your email address and we'll send you a link to reset your password.
        </p>
        <form method="post">
            <label for="email">Email</label>
            <input type="email" id="email" name="email" required autofocus
                   autocomplete="email">
            <button type="submit">Send Reset Link</button>
        </form>
        <div class="links"><a href="login.php">Back to login</a></div>
    <?php endif; ?>
</div>
</body>
</html>
