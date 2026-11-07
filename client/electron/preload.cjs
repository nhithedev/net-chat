const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('api', {
  sendToC: (msg) => ipcRenderer.send('send-to-c', msg),
  onCData: (cb) => ipcRenderer.on('c-data', (_e, data) => cb(data)),
});