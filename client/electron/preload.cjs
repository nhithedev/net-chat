// Cây cầu duy nhất giữa giao diện và Electron main (protocol-v2.md mục 10.4).
// Chỉ phơi các hàm dưới đây, không phơi ipcRenderer thô.
const { contextBridge, ipcRenderer } = require('electron')

function subscribe(channel, cb) {
  const handler = (_e, payload) => cb(payload)
  ipcRenderer.on(channel, handler)
  return () => ipcRenderer.removeListener(channel, handler)
}

contextBridge.exposeInMainWorld('netchat', {
  connect: (opts) => ipcRenderer.invoke('netchat:connect', opts),
  disconnect: () => ipcRenderer.invoke('netchat:disconnect'),
  request: (type, data) => ipcRenderer.invoke('netchat:request', type, data),
  onEvent: (cb) => subscribe('netchat:event', cb),
  onStatus: (cb) => subscribe('netchat:status', cb),
})
