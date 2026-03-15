<?php
/**
 * client.php - xterm.js web client for deMUSE
 *
 * Receives a one-time auth token via URL parameter and automatically
 * authenticates over WebSocket. Redirects to login page on disconnect
 * since the token is consumed and can't be reused.
 */

$token = $_GET['token'] ?? '';
if (!$token) {
    header('Location: login.php');
    exit;
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>deMUSE</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/css/xterm.min.css">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        html, body { height: 100%; background: #000; overflow: hidden; }
        #terminal { width: 100%; height: 100%; }
        #status {
            position: fixed;
            top: 4px;
            right: 8px;
            font-family: monospace;
            font-size: 12px;
            color: #666;
            z-index: 100;
        }
        #status.connected { color: #0a0; }
        #status.disconnected { color: #a00; }
    </style>
</head>
<body>
    <div id="status" class="disconnected">disconnected</div>
    <div id="terminal"></div>

    <script src="https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/lib/xterm.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/@xterm/addon-fit@0.10.0/lib/addon-fit.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/@xterm/addon-web-links@0.11.0/lib/addon-web-links.min.js"></script>
    <script>
    (function() {
        'use strict';

        var authToken = <?= json_encode($token) ?>;
        var tokenSent = false;

        var term = new Terminal({
            cursorBlink: true,
            fontSize: 14,
            fontFamily: '"Fira Code", "Cascadia Code", "Consolas", monospace',
            theme: {
                background: '#000000',
                foreground: '#c0c0c0',
                cursor: '#c0c0c0'
            },
            scrollback: 10000,
            convertEol: true
        });

        var fitAddon = new FitAddon.FitAddon();
        var webLinksAddon = new WebLinksAddon.WebLinksAddon();

        term.loadAddon(fitAddon);
        term.loadAddon(webLinksAddon);

        var container = document.getElementById('terminal');
        var statusEl = document.getElementById('status');
        term.open(container);
        fitAddon.fit();

        var ws = null;
        var inputLine = '';
        var waitingForReconnect = false;

        function setStatus(text, cls) {
            statusEl.textContent = text;
            statusEl.className = cls;
        }

        function connect() {
            if (ws && ws.readyState <= 1) return;

            var proto = (location.protocol === 'https:') ? 'wss:' : 'ws:';
            var url = proto + '//' + location.host + '/ws';

            setStatus('connecting...', 'disconnected');
            ws = new WebSocket(url, 'demuse');

            ws.onopen = function() {
                setStatus('connected', 'connected');
                inputLine = '';

                /* Send auth token automatically on first connect */
                if (authToken && !tokenSent) {
                    ws.send('connect token:' + authToken + '\r\n');
                    tokenSent = true;
                    authToken = null;  /* Clear from memory */
                }
            };

            ws.onmessage = function(ev) {
                term.write(ev.data);
            };

            ws.onclose = function() {
                setStatus('disconnected \u2014 press any key to reconnect', 'disconnected');
                term.write('\r\n\x1b[31m--- Connection closed \u2014 press any key to reconnect ---\x1b[0m\r\n');
                ws = null;
                waitingForReconnect = true;
            };

            ws.onerror = function() {
                /* onclose will fire after this */
            };
        }

        term.onData(function(data) {
            if (waitingForReconnect) {
                waitingForReconnect = false;
                /* Token is consumed — redirect to login for a new one */
                window.location.href = 'login.php';
                return;
            }
            if (!ws || ws.readyState !== 1) return;

            for (var i = 0; i < data.length; i++) {
                var ch = data.charCodeAt(i);

                if (ch === 13 || ch === 10) {
                    term.write('\r\n');
                    ws.send(inputLine + '\r\n');
                    inputLine = '';
                } else if (ch === 127 || ch === 8) {
                    if (inputLine.length > 0) {
                        inputLine = inputLine.slice(0, -1);
                        term.write('\b \b');
                    }
                } else if (ch === 21) {
                    while (inputLine.length > 0) {
                        inputLine = inputLine.slice(0, -1);
                        term.write('\b \b');
                    }
                } else if (ch >= 32) {
                    inputLine += data[i];
                    term.write(data[i]);
                }
            }
        });

        /* Handle paste */
        term.onData(function() {}); /* already handled above */

        window.addEventListener('resize', function() {
            fitAddon.fit();
        });

        connect();
    })();
    </script>
</body>
</html>
