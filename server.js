const express = require('express');
const mongoose = require('mongoose');
const axios = require('axios');
const path = require('path');

const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// ---- MongoDB Connection ----
mongoose.connect('mongodb://localhost:27017/smart_env', {
  useNewUrlParser: true,
  useUnifiedTopology: true,
});
mongoose.connection.on('connected', () => console.log('MongoDB connected'));
mongoose.connection.on('error', (err) => console.error('MongoDB error:', err));

// ---- Schemas ----
const SensorData = mongoose.model('sensor_data', new mongoose.Schema({
  temperature: Number,
  humidity: Number,
  light: Number,
  motion: Boolean,
  timestamp: { type: Date, default: Date.now },
}));

const DeviceStatus = mongoose.model('device_status', new mongoose.Schema({
  led: { type: Boolean, default: false },
  servo: { type: Boolean, default: false },
  buzzer: { type: Boolean, default: false },
  updated_at: { type: Date, default: Date.now },
}));

// ---- Telegram Config ----
const TELEGRAM_BOT_TOKEN = '7237491284:AAHbiAeZ61iZRX0Dl-0wGjD85_l8xADDCSU';
const TELEGRAM_CHAT_ID = '6675198298';

// Alert cooldown: เก็บเวลาที่แจ้งล่าสุดต่อ event
const lastAlert = { temperature: 0, motion: 0 };
const ALERT_COOLDOWN = 5 * 60 * 1000; // 5 นาที

async function sendTelegram(message) {
  try {
    await axios.post(`https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage`, {
      chat_id: TELEGRAM_CHAT_ID,
      text: message,
    });
    console.log('Telegram sent:', message);
  } catch (err) {
    console.error('Telegram error:', err.message);
  }
}

// ---- Rate Limit สำหรับ /api/control ----
let lastControlTime = 0;
const CONTROL_LIMIT_MS = 2000; // 2 วินาที

// ---- API Routes ----

// POST /api/data - รับข้อมูลจาก ESP32
app.post('/api/data', async (req, res) => {
  const { temperature, humidity, light, motion } = req.body;

  if (temperature === undefined || humidity === undefined || light === undefined || motion === undefined) {
    return res.status(400).json({ error: 'Missing fields' });
  }

  try {
    // บันทึกข้อมูล
    await SensorData.create({ temperature, humidity, light, motion });

    // ตรวจ Telegram Alert
    const now = Date.now();

    if (temperature > 35 && now - lastAlert.temperature > ALERT_COOLDOWN) {
      lastAlert.temperature = now;
      sendTelegram(`🌡️ แจ้งเตือน! อุณหภูมิสูง: ${temperature}°C`);
    }

    if (motion === true && now - lastAlert.motion > ALERT_COOLDOWN) {
      lastAlert.motion = now;
      sendTelegram(`🚶 แจ้งเตือน! ตรวจพบการเคลื่อนไหว`);
    }

    res.json({ success: true });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Server error' });
  }
});

// GET /api/data - ข้อมูลล่าสุด
app.get('/api/data', async (req, res) => {
  try {
    const latest = await SensorData.findOne().sort({ timestamp: -1 });
    res.json(latest || {});
  } catch (err) {
    res.status(500).json({ error: 'Server error' });
  }
});

// GET /api/history - ข้อมูลย้อนหลัง 50 รายการ
app.get('/api/history', async (req, res) => {
  try {
    const history = await SensorData.find().sort({ timestamp: -1 }).limit(50);
    res.json(history);
  } catch (err) {
    res.status(500).json({ error: 'Server error' });
  }
});

// POST /api/control - ควบคุมอุปกรณ์
app.post('/api/control', async (req, res) => {
  const now = Date.now();
  if (now - lastControlTime < CONTROL_LIMIT_MS) {
    return res.status(429).json({ error: 'Rate limit: กดเร็วเกินไป' });
  }
  lastControlTime = now;

  const { led, servo, buzzer } = req.body;

  try {
    let status = await DeviceStatus.findOne();
    if (!status) status = new DeviceStatus();

    if (led !== undefined) status.led = led;
    if (servo !== undefined) status.servo = servo;
    if (buzzer !== undefined) status.buzzer = buzzer;
    status.updated_at = new Date();
    await status.save();

    res.json({ success: true, status });
  } catch (err) {
    res.status(500).json({ error: 'Server error' });
  }
});

// GET /api/status - ESP32 ดึงสถานะอุปกรณ์ล่าสุด
app.get('/api/status', async (req, res) => {
  try {
    const status = await DeviceStatus.findOne();
    res.json(status || { led: false, servo: false, buzzer: false });
  } catch (err) {
    res.status(500).json({ error: 'Server error' });
  }
});

// ---- Start Server ----
const PORT = 3000;
app.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}`);
});
