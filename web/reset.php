<?php
/**
 * reset.php - Set new password for deMUSE account
 *
 * Validates the reset token from the email link.
 * On valid token, shows a form to set a new password.
 * On submit, updates the password and deletes the token.
 */

require_once __DIR__ . '/db.php';

$error = '';
$success = '';
$token = $_GET['token'] ?? $_POST['token'] ?? '';
$valid_token = false;

if (!$token) {
    $error = 'No reset token provided.';
} else {
    $pdo = get_pdo();

    /* Validate token */
    $stmt = $pdo->prepare(
        'SELECT t.account_id, a.username
         FROM password_reset_tokens t
         JOIN accounts a ON a.account_id = t.account_id
         WHERE t.token = :t AND t.expires_at > NOW()'
    );
    $stmt->execute(['t' => $token]);
    $row = $stmt->fetch();

    if (!$row) {
        $error = 'Invalid or expired reset link. Please request a new one.';
    } else {
        $valid_token = true;

        /* Handle password submission */
        if ($_SERVER['REQUEST_METHOD'] === 'POST') {
            $password = $_POST['password'] ?? '';
            $confirm  = $_POST['confirm'] ?? '';

            if (strlen($password) < 8) {
                $error = 'Password must be at least 8 characters.';
            } elseif ($password !== $confirm) {
                $error = 'Passwords do not match.';
            } else {
                /* Update password */
                $hash = password_hash($password, PASSWORD_BCRYPT);
                $stmt = $pdo->prepare(
                    'UPDATE accounts SET password_hash = :h WHERE account_id = :a'
                );
                $stmt->execute([
                    'h' => $hash,
                    'a' => $row['account_id'],
                ]);

                /* Delete all reset tokens for this account */
                $stmt = $pdo->prepare(
                    'DELETE FROM password_reset_tokens WHERE account_id = :a'
                );
                $stmt->execute(['a' => $row['account_id']]);

                $success = 'Password updated! You can now log in.';
                $valid_token = false;
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
    <title>deMUSE - Set New Password</title>
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
        input[type="password"] {
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
    <h1>Set New Password</h1>
    <?php if ($error): ?>
        <div class="error"><?= htmlspecialchars($error) ?></div>
        <div class="links"><a href="forgot.php">Request a new reset link</a></div>
    <?php elseif ($success): ?>
        <div class="success"><?= htmlspecialchars($success) ?></div>
        <div class="links"><a href="login.php">Log in</a></div>
    <?php elseif ($valid_token): ?>
        <form method="post">
            <input type="hidden" name="token" value="<?= htmlspecialchars($token) ?>">
            <label for="password">New Password</label>
            <input type="password" id="password" name="password" required
                   minlength="8" autocomplete="new-password" autofocus>
            <label for="confirm">Confirm Password</label>
            <input type="password" id="confirm" name="confirm" required
                   minlength="8" autocomplete="new-password">
            <button type="submit">Set Password</button>
        </form>
        <div class="links"><a href="login.php">Back to login</a></div>
    <?php endif; ?>
</div>
</body>
</html>
