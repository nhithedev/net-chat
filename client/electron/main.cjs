const { app, BrowserWindow, ipcMain } = require('electron')
const fs = require('fs')
const path = require('path')
const { createCoreLink } = require('./core-link.cjs')

// Đường dẫn lõi mạng C: biến NETCHAT_CORE, nếu không có thì bản build mặc định.
function resolveCorePath() {
  if (process.env.NETCHAT_CORE) return process.env.NETCHAT_CORE
  const exe = process.platform === 'win32' ? 'bin/netchat-core.exe' : 'build/netchat-core'
  return path.join(__dirname, '../native', exe)
}

let win = null
let link = null

function send(channel, payload) {
  if (win && !win.isDestroyed()) win.webContents.send(channel, payload)
}

function setupCoreLink() {
  const corePath = resolveCorePath()
  if (!fs.existsSync(corePath)) {
    console.error(`[netchat] không thấy lõi mạng tại ${corePath} (chạy "make" trong client/native)`)
  }
  link = createCoreLink({
    corePath,
    onEvent: (evt) => send('netchat:event', evt),
    onStatus: (s) => send('netchat:status', s),
    onLog: (line) => console.log('[netchat]', line),
  })

  // Renderer chỉ gọi những kênh này qua preload; không có quyền ra mạng.
  ipcMain.handle('netchat:connect', (_e, opts) => link.connect(opts))
  ipcMain.handle('netchat:disconnect', () => link.disconnect())
  ipcMain.handle('netchat:request', (_e, type, data) => link.request(type, data))
}

function createWindow() {
  win = new BrowserWindow({
    width: 1000,
    height: 700,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  })

  if (process.env.ELECTRON_DEV) {
    win.loadURL('http://localhost:5173')
  } else {
    win.loadFile(path.join(__dirname, '../dist/index.html')) // Vite: ../dist/index.html
  }
}

app.whenReady().then(() => {
  setupCoreLink()
  createWindow()
})

app.on('before-quit', () => link?.stop())

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})
