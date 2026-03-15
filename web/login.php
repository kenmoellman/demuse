<?php
/**
 * login.php - Login page for deMUSE
 *
 * Authenticates against the accounts table, generates a one-time token,
 * and presents options for browser (WebSocket) or telnet connection.
 */

require_once __DIR__ . '/db.php';

session_start();

$error = '';
$token = null;
$token_type = null;

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $username = trim($_POST['username'] ?? '');
    $password = $_POST['password'] ?? '';
    $connect_via = $_POST['connect_via'] ?? 'websocket';

    if (!$username || !$password) {
        $error = 'Please enter your username and password.';
    } else {
        $pdo = get_pdo();

        /* Look up account */
        $stmt = $pdo->prepare(
            'SELECT account_id, password_hash, is_verified, is_disabled
             FROM accounts WHERE username = :u'
        );
        $stmt->execute(['u' => $username]);
        $account = $stmt->fetch();

        if (!$account || !password_verify($password, $account['password_hash'])) {
            $error = 'Invalid username or password.';
        } elseif (!$account['is_verified']) {
            $error = 'Your account has not been verified. Please check your email.';
        } elseif ($account['is_disabled']) {
            $error = 'Your account has been disabled. Please contact an administrator.';
        } else {
            /* Authentication successful — generate one-time token */
            $token = bin2hex(random_bytes(32));
            $token_type = ($connect_via === 'telnet') ? 'telnet' : 'websocket';

            $stmt = $pdo->prepare(
                'INSERT INTO auth_tokens (account_id, token, token_type, expires_at)
                 VALUES (:a, :t, :tt, DATE_ADD(NOW(), INTERVAL 60 SECOND))'
            );
            $stmt->execute([
                'a'  => $account['account_id'],
                't'  => $token,
                'tt' => $token_type,
            ]);

            /* Update last login time */
            $stmt = $pdo->prepare('UPDATE accounts SET last_login = NOW() WHERE account_id = :a');
            $stmt->execute(['a' => $account['account_id']]);
        }
    }
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>deMUSE - Login</title>
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
        input[type="text"], input[type="password"] {
            width: 100%; padding: 0.6rem; margin-bottom: 1rem;
            background: #0f3460; border: 1px solid #333; border-radius: 4px;
            color: #e0e0e0; font-size: 1rem;
        }
        input:focus { outline: none; border-color: #00a8cc; }
        button, .btn {
            display: inline-block; width: 100%; padding: 0.7rem;
            background: #00a8cc; color: #fff; border: none; border-radius: 4px;
            font-size: 1rem; cursor: pointer; text-align: center;
            text-decoration: none; margin-bottom: 0.5rem;
        }
        button:hover, .btn:hover { background: #0090b0; }
        .error { color: #ff6b6b; margin-bottom: 1rem; font-size: 0.9rem; }
        .links { margin-top: 1rem; text-align: center; font-size: 0.9rem; }
        .links a { color: #00a8cc; text-decoration: none; }
        .links a:hover { text-decoration: underline; }
        .token-box {
            background: #0f3460; padding: 1rem; border-radius: 4px;
            font-family: monospace; font-size: 0.95rem; color: #51cf66;
            word-break: break-all; margin: 1rem 0; cursor: pointer;
            border: 1px solid #333; text-align: center;
        }
        .token-box:hover { border-color: #00a8cc; }
        .hint { font-size: 0.8rem; color: #888; margin-bottom: 1rem; text-align: center; }
        .radio-group {
            display: flex; gap: 1rem; margin-bottom: 1rem;
            justify-content: center;
        }
        .radio-group label {
            display: flex; align-items: center; gap: 0.3rem;
            cursor: pointer; color: #c0c0c0;
        }
    </style>
</head>
<body>
<div class="container">
    <h1>deMUSE Login</h1>

    <?php if ($token && $token_type === 'websocket'): ?>
        <p style="text-align:center; margin-bottom:1rem;">Authentication successful. Connecting...</p>
        <a class="btn" href="client.php?token=<?= urlencode($token) ?>">Connect via Browser</a>
        <div class="links"><a href="login.php">Back to login</a></div>

    <?php elseif ($token && $token_type === 'telnet'): ?>
        <p style="text-align:center; margin-bottom:0.5rem;">Paste this into your MUD client:</p>
        <div class="token-box" onclick="navigator.clipboard.writeText(this.textContent).then(()=>{this.style.borderColor='#51cf66'})">connect token:<?= htmlspecialchars($token) ?></div>
        <p class="hint">Click to copy. Token expires in 60 seconds.</p>
        <div class="links"><a href="login.php">Back to login</a></div>

    <?php else: ?>
        <?php if ($error): ?>
            <div class="error"><?= htmlspecialchars($error) ?></div>
        <?php endif; ?>
        <form method="post">
            <label for="username">Username</label>
            <input type="text" id="username" name="username" required
                   value="<?= htmlspecialchars($username ?? '') ?>"
                   autocomplete="username" autofocus>

            <label for="password">Password</label>
            <input type="password" id="password" name="password" required
                   autocomplete="current-password">

            <div class="radio-group">
                <label><input type="radio" name="connect_via" value="websocket" checked> Browser</label>
                <label><input type="radio" name="connect_via" value="telnet"> MUD Client</label>
            </div>

            <button type="submit">Log In</button>
        </form>
        <div class="links">
            <a href="forgot.php">Forgot password?</a> |
            <a href="register.php">Create an account</a>
        </div>
    <?php endif; ?>
</div>
</body>
</html>
