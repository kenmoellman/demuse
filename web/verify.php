<?php
/**
 * verify.php - Email verification for deMUSE accounts
 *
 * Validates the token from the verification email link.
 * On success, marks the account as verified.
 */

require_once __DIR__ . '/db.php';

$error = '';
$success = '';

$token = $_GET['token'] ?? '';

if (!$token) {
    $error = 'No verification token provided.';
} else {
    $pdo = get_pdo();

    /* Look up token, check expiry */
    $stmt = $pdo->prepare(
        'SELECT t.account_id, a.username
         FROM email_verify_tokens t
         JOIN accounts a ON a.account_id = t.account_id
         WHERE t.token = :t AND t.expires_at > NOW()'
    );
    $stmt->execute(['t' => $token]);
    $row = $stmt->fetch();

    if (!$row) {
        $error = 'Invalid or expired verification link. Please register again.';
    } else {
        /* Mark account as verified */
        $stmt = $pdo->prepare('UPDATE accounts SET is_verified = 1 WHERE account_id = :a');
        $stmt->execute(['a' => $row['account_id']]);

        /* Delete all verification tokens for this account */
        $stmt = $pdo->prepare('DELETE FROM email_verify_tokens WHERE account_id = :a');
        $stmt->execute(['a' => $row['account_id']]);

        $success = 'Your account has been verified! You can now log in.';
    }
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>deMUSE - Verify Account</title>
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
            text-align: center;
        }
        h1 { color: #e0e0e0; margin-bottom: 1.5rem; }
        .error { color: #ff6b6b; margin-bottom: 1rem; }
        .success { color: #51cf66; margin-bottom: 1rem; }
        .links { margin-top: 1rem; }
        .links a { color: #00a8cc; text-decoration: none; }
        .links a:hover { text-decoration: underline; }
    </style>
</head>
<body>
<div class="container">
    <h1>Account Verification</h1>
    <?php if ($error): ?>
        <div class="error"><?= htmlspecialchars($error) ?></div>
        <div class="links"><a href="register.php">Register again</a></div>
    <?php else: ?>
        <div class="success"><?= htmlspecialchars($success) ?></div>
        <div class="links"><a href="login.php">Log in</a></div>
    <?php endif; ?>
</div>
</body>
</html>
