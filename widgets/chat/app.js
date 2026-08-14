window.backend = null;

window.updateHoleRect = function(x, y, width, height, viewWidth) {
    const spacer = document.getElementById('hole-spacer');
    const border = document.getElementById('hole-border');

    if (!spacer || !border) return;

    if (width <= 0 || height <= 0) {
        spacer.style.display = 'none';
        border.style.display = 'none';
        return;
    }

    const isRightSide = (x + width / 2) > (viewWidth / 2);
    const floatDir = isRightSide ? 'right' : 'left';

    spacer.style.display = 'block';
    spacer.style.float = floatDir;
    spacer.style.width = width + 'px';
    spacer.style.height = height + 'px';
    spacer.style.shapeOutside = 'inset(0px 0px 0px 0px)';
    spacer.style.marginTop = Math.max(0, y) + 'px';

    border.style.display = 'block';
    border.style.left = x + 'px';
    border.style.top = y + 'px';
    border.style.width = width + 'px';
    border.style.height = height + 'px';

    scrollToBottom();
};

function setInputValue(text) {
    const input = document.getElementById('message-input');
    if (input) {
        input.value = text;
        autoResizeInput(input);
    }
}

function setCaptureState(text, enabled, stateClass) {
    const btn = document.getElementById('capture-btn');
    if (!btn) return;
    btn.textContent = text;
    btn.disabled = !enabled;
    btn.className = 'action-btn ' + stateClass;
}

function setMicState(text) {
    const btn = document.getElementById('mic-btn');
    if (btn) btn.textContent = text;
}

function sendMessage() {
    const input = document.getElementById('message-input');
    const text = input.value.trim();

    if (!text) return;

    input.value = '';
    autoResizeInput(input);

    document.getElementById('root-wrapper').classList.remove('expanded-input');
    document.getElementById('expand-btn').textContent = '⤢';

    if (window.backend) {
        if (typeof window.backend.onUserSendMessage === 'function') {
            window.backend.onUserSendMessage(text);
        } else if (typeof window.backend.sendMessage === 'function') {
            window.backend.sendMessage(text);
        }
    }
}

function attachCopyUtils(bubble) {
    bubble.querySelectorAll('pre').forEach((pre) => {
        if (pre.querySelector('.code-copy-btn')) return;

        const copyBtn = document.createElement('button');
        copyBtn.className = 'code-copy-btn';
        copyBtn.textContent = 'Copy';

        copyBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            const code = pre.querySelector('code');
            const textToCopy = code ? code.innerText : pre.innerText;

            navigator.clipboard.writeText(textToCopy).then(() => {
                copyBtn.textContent = 'Copied!';
                copyBtn.classList.add('copied');
                setTimeout(() => {
                    copyBtn.textContent = 'Copy';
                    copyBtn.classList.remove('copied');
                }, 2000);
            });
        });

        pre.appendChild(copyBtn);
    });
}

function createMsgCopyButton(textToCopy) {
    const btn = document.createElement('button');
    btn.className = 'msg-copy-btn';
    btn.textContent = 'Copy text';

    btn.addEventListener('click', () => {
        navigator.clipboard.writeText(textToCopy).then(() => {
            btn.textContent = 'Copied text!';
            setTimeout(() => {
                btn.textContent = 'Copy text';
            }, 2000);
        });
    });

    return btn;
}

function appendMessage(text, isUser) {
    const messagesDiv = document.getElementById('messages');
    if (!messagesDiv) return;

    const row = document.createElement('div');
    row.className = 'message-row ' + (isUser ? 'user' : 'ai');

    const bubble = document.createElement('div');
    bubble.className = 'bubble';
    bubble.dataset.rawText = text;

    bubble.innerHTML = marked.parse(text);
    bubble.querySelectorAll('pre code').forEach((block) => {
        hljs.highlightElement(block);
    });

    attachCopyUtils(bubble);

    const msgCopyBtn = createMsgCopyButton(text);

    row.appendChild(bubble);
    row.appendChild(msgCopyBtn);
    messagesDiv.appendChild(row);

    scrollToBottom();
}

function appendToLastAiMessage(deltaText) {
    const messagesDiv = document.getElementById('messages');
    if (!messagesDiv) return;

    const aiRows = messagesDiv.getElementsByClassName('message-row ai');
    if (aiRows.length === 0) {
        appendMessage(deltaText, false);
        return;
    }

    const lastAiRow = aiRows[aiRows.length - 1];
    const bubble = lastAiRow.querySelector('.bubble');
    if (bubble) {
        if (!bubble.dataset.rawText) {
            bubble.dataset.rawText = bubble.textContent || "";
        }
        bubble.dataset.rawText += deltaText;
        bubble.innerHTML = marked.parse(bubble.dataset.rawText);
        bubble.querySelectorAll('pre code').forEach((block) => {
            hljs.highlightElement(block);
        });

        attachCopyUtils(bubble);

        let msgCopyBtn = lastAiRow.querySelector('.msg-copy-btn');
        if (msgCopyBtn) {
            msgCopyBtn.replaceWith(createMsgCopyButton(bubble.dataset.rawText));
        } else {
            lastAiRow.appendChild(createMsgCopyButton(bubble.dataset.rawText));
        }
    }

    scrollToBottom();
}

function scrollToBottom() {
    const container = document.getElementById('chat-container');
    if (container) {
        container.scrollTop = container.scrollHeight;
    }
}

function autoResizeInput(textarea) {
    if (document.getElementById('root-wrapper').classList.contains('expanded-input')) return;
    textarea.style.height = 'auto';
    textarea.style.height = Math.min(textarea.scrollHeight, 250) + 'px';
}

document.addEventListener("DOMContentLoaded", function () {
    const sendBtn = document.getElementById('send-btn');
    const captureBtn = document.getElementById('capture-btn');
    const micBtn = document.getElementById('mic-btn');
    const expandBtn = document.getElementById('expand-btn');
    const input = document.getElementById('message-input');
    const wrapper = document.getElementById('root-wrapper');

    input.addEventListener('input', function() {
        autoResizeInput(this);
    });

    expandBtn.addEventListener('click', function() {
        wrapper.classList.toggle('expanded-input');
        const isExpanded = wrapper.classList.contains('expanded-input');
        expandBtn.textContent = isExpanded ? '⤡' : '⤢';
        if (!isExpanded) {
            autoResizeInput(input);
        } else {
            input.style.height = '';
        }
    });

    sendBtn.addEventListener('click', function() {
        sendMessage();
    });

    captureBtn.addEventListener('click', function() {
        if (window.backend && typeof window.backend.requestCapture === 'function') {
            window.backend.requestCapture();
        }
    });

    micBtn.addEventListener('click', function() {
        if (window.backend && typeof window.backend.requestToggleMic === 'function') {
            window.backend.requestToggleMic();
        }
    });

    input.addEventListener('keydown', function(e) {
        if (e.key === 'Enter' && (e.ctrlKey || e.metaKey || wrapper.classList.contains('expanded-input'))) {
            return;
        }
        if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            sendMessage();
        }
    });

    if (typeof QWebChannel !== "undefined") {
        new QWebChannel(qt.webChannelTransport, function (channel) {
            window.backend = channel.objects.backend;
        });
    }
});