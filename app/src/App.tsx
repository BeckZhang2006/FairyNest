/**
 * FairyNest Frontend - Device Management Dashboard
 * React + TypeScript + Tailwind CSS + shadcn/ui
 */

import { useState, useEffect, useRef } from 'react'
import { Tabs, TabsList, TabsTrigger } from '@/components/ui/tabs'
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Slider } from '@/components/ui/slider'
import { Switch } from '@/components/ui/switch'
import { Label } from '@/components/ui/label'
import { Badge } from '@/components/ui/badge'
import { Separator } from '@/components/ui/separator'
import { toast } from 'sonner'
import {
  Moon, Sun, Mic, Bell, Wifi, Activity, Settings,
  Home, User, Lightbulb, Plus, Trash2, Power
} from 'lucide-react'
import './App.css'

// ============== TYPES ==============
interface Device {
  device_id: string
  name: string
  is_online: boolean
  wifi_rssi: number
  firmware_version: string
}

interface Alarm {
  index: number
  enabled: boolean
  hour: number
  minute: number
  days: number
  label: string
}

interface VoiceLog {
  time: string
  text: string
  type: 'user' | 'assistant'
}

interface CSIPoint {
  time: number
  variance: number
  presence: number
}



// ============== UTILS ==============
function formatDays(days: number): string {
  const dayNames = ['日', '一', '二', '三', '四', '五', '六']
  const active: string[] = []
  for (let i = 0; i < 7; i++) {
    if (days & (1 << i)) active.push(dayNames[i])
  }
  if (active.length === 7) return '每天'
  if (active.length === 0) return '一次'
  return active.join(' ')
}

// ============== CANVAS CHART COMPONENT ==============
function CSIChart({ data, threshold }: { data: CSIPoint[]; threshold: number }) {
  const canvasRef = useRef<HTMLCanvasElement>(null)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return

    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const dpr = window.devicePixelRatio || 1
    const rect = canvas.getBoundingClientRect()
    canvas.width = rect.width * dpr
    canvas.height = rect.height * dpr
    ctx.scale(dpr, dpr)

    const width = rect.width
    const height = rect.height

    ctx.clearRect(0, 0, width, height)

    // Draw grid
    ctx.strokeStyle = '#e7e5e4'
    ctx.lineWidth = 1
    for (let i = 0; i < 5; i++) {
      const y = (height / 4) * i
      ctx.beginPath()
      ctx.moveTo(0, y)
      ctx.lineTo(width, y)
      ctx.stroke()
    }

    // Draw CSI variance line
    const maxVariance = Math.max(...data.map((d) => d.variance))
    const minVariance = Math.min(...data.map((d) => d.variance))
    const range = maxVariance - minVariance || 1

    ctx.strokeStyle = '#f97316'
    ctx.lineWidth = 2
    ctx.beginPath()
    data.forEach((point, i) => {
      const x = (i / (data.length - 1)) * width
      const y = height - ((point.variance - minVariance) / range) * height * 0.8 - height * 0.1
      if (i === 0) ctx.moveTo(x, y)
      else ctx.lineTo(x, y)
    })
    ctx.stroke()

    // Draw threshold line
    const thresholdY = height - ((threshold - minVariance) / range) * height * 0.8 - height * 0.1
    ctx.strokeStyle = '#ef4444'
    ctx.setLineDash([5, 5])
    ctx.beginPath()
    ctx.moveTo(0, thresholdY)
    ctx.lineTo(width, thresholdY)
    ctx.stroke()
    ctx.setLineDash([])

    // Label
    ctx.fillStyle = '#ef4444'
    ctx.font = '12px sans-serif'
    ctx.fillText(`阈值: ${threshold}`, 10, thresholdY - 5)
  }, [data, threshold])

  return <canvas ref={canvasRef} className="w-full h-48" />
}

// ============== MAIN APP ==============
function App() {
  const [activeTab, setActiveTab] = useState('dashboard')
  const [devices, setDevices] = useState<Device[]>([])
  const [alarms, setAlarms] = useState<Alarm[]>([])
  const [csiHistory, setCsiHistory] = useState<CSIPoint[]>([])
  const [voiceLogs] = useState<VoiceLog[]>([])
  const [csiThreshold, setCsiThreshold] = useState(15)
  const [ledBrightness, setLedBrightness] = useState([20])
  const [isLightOn, setIsLightOn] = useState(false)
  const [isNightMode, setIsNightMode] = useState(false)

  // Fetch devices from backend
  useEffect(() => {
    const fetchDevices = async () => {
      try {
        const res = await fetch('/api/devices')
        const data = await res.json()
        setDevices(
          (data.devices || []).map((d: any) => ({
            device_id: d.device_id,
            name: d.name || d.device_id,
            is_online: d.is_online ?? false,
            wifi_rssi: d.wifi_rssi ?? 0,
            firmware_version: d.firmware_version || '',
          }))
        )
      } catch (e) {
        console.error('Failed to fetch devices:', e)
      }
    }
    fetchDevices()
    const interval = setInterval(fetchDevices, 3000)
    return () => clearInterval(interval)
  }, [])

  // Fetch alarms for first device
  useEffect(() => {
    const fetchAlarms = async () => {
      if (devices.length === 0) {
        setAlarms([])
        return
      }
      try {
        const res = await fetch(`/api/alarms/${devices[0].device_id}`)
        const data = await res.json()
        setAlarms(data.alarms || [])
      } catch (e) {
        console.error('Failed to fetch alarms:', e)
      }
    }
    fetchAlarms()
  }, [devices])

  // Fetch CSI data for first device
  useEffect(() => {
    const fetchCSI = async () => {
      if (devices.length === 0) {
        setCsiHistory([])
        return
      }
      try {
        const res = await fetch(`/api/csi/${devices[0].device_id}?limit=60`)
        const data = await res.json()
        setCsiHistory(data.data || [])
      } catch (e) {
        console.error('Failed to fetch CSI:', e)
      }
    }
    fetchCSI()
    const interval = setInterval(fetchCSI, 3000)
    return () => clearInterval(interval)
  }, [devices])

  useEffect(() => {
    const hour = new Date().getHours()
    setIsNightMode(hour >= 22 || hour < 6)
  }, [])

  const addAlarm = () => {
    const newAlarm: Alarm = {
      index: alarms.length,
      enabled: true,
      hour: 8,
      minute: 0,
      days: 0b0111110,
      label: '新闹钟',
    }
    setAlarms([...alarms, newAlarm])
    toast.success('闹钟已添加')
  }

  const updateAlarm = (index: number, updates: Partial<Alarm>) => {
    setAlarms(alarms.map((a) => (a.index === index ? { ...a, ...updates } : a)))
  }

  const deleteAlarm = (index: number) => {
    setAlarms(alarms.filter((a) => a.index !== index))
    toast.success('闹钟已删除')
  }

  const currentTime = new Date().toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })
  const nextAlarm = alarms.find((a) => a.enabled)

  return (
    <div className="min-h-screen bg-gradient-to-br from-stone-50 to-orange-50/30">
      {/* Header */}
      <header className="bg-white/80 backdrop-blur-md border-b border-stone-200 sticky top-0 z-50">
        <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
          <div className="flex items-center justify-between h-16">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-xl bg-gradient-to-br from-orange-400 to-orange-600 flex items-center justify-center text-white">
                <Moon size={20} />
              </div>
              <div>
                <h1 className="text-xl font-bold text-stone-900">FairyNest</h1>
                <p className="text-xs text-stone-500">智能床头终端管理</p>
              </div>
            </div>
            <div className="flex items-center gap-4">
              <div className="flex items-center gap-2 px-3 py-1 rounded-full bg-green-50 text-green-700 text-sm">
                <div className="w-2 h-2 rounded-full bg-green-500 animate-pulse" />
                服务在线
              </div>
              <div className="w-8 h-8 rounded-full bg-orange-100 flex items-center justify-center text-orange-700">
                <User size={16} />
              </div>
            </div>
          </div>
        </div>
      </header>

      {/* Main Content */}
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-6">
        <div className="flex gap-6">
          {/* Sidebar */}
          <nav className="w-56 flex-shrink-0 hidden md:block">
            <div className="bg-white rounded-2xl shadow-sm p-3 sticky top-24 space-y-1">
              <SidebarButton icon={<Home size={18} />} label="总览" active={activeTab === 'dashboard'} onClick={() => setActiveTab('dashboard')} />
              <SidebarButton icon={<Lightbulb size={18} />} label="灯光控制" active={activeTab === 'light'} onClick={() => setActiveTab('light')} />
              <SidebarButton icon={<Bell size={18} />} label="闹钟管理" active={activeTab === 'alarm'} onClick={() => setActiveTab('alarm')} />
              <SidebarButton icon={<Activity size={18} />} label="CSI检测" active={activeTab === 'csi'} onClick={() => setActiveTab('csi')} />
              <SidebarButton icon={<Mic size={18} />} label="语音记录" active={activeTab === 'voice'} onClick={() => setActiveTab('voice')} />
              <SidebarButton icon={<Settings size={18} />} label="设备设置" active={activeTab === 'settings'} onClick={() => setActiveTab('settings')} />
            </div>

            {/* Device Status Card */}
            <div className="bg-white rounded-2xl shadow-sm p-4 mt-4">
              <h3 className="text-sm font-semibold text-stone-700 mb-3">设备状态</h3>
              {devices.map((device) => (
                <div key={device.device_id} className="space-y-2">
                  <div className="flex items-center justify-between">
                    <span className="text-sm font-medium">{device.name}</span>
                    <Badge variant={device.is_online ? 'default' : 'destructive'} className={device.is_online ? 'bg-green-100 text-green-700 hover:bg-green-100' : ''}>
                      {device.is_online ? '在线' : '离线'}
                    </Badge>
                  </div>
                  {device.is_online && (
                    <div className="flex items-center gap-1 text-xs text-stone-500">
                      <Wifi size={12} />
                      {device.wifi_rssi} dBm
                    </div>
                  )}
                </div>
              ))}
            </div>
          </nav>

          {/* Mobile Tabs */}
          <div className="md:hidden w-full mb-4">
            <Tabs value={activeTab} onValueChange={setActiveTab}>
              <TabsList className="w-full grid grid-cols-3">
                <TabsTrigger value="dashboard">总览</TabsTrigger>
                <TabsTrigger value="light">灯光</TabsTrigger>
                <TabsTrigger value="alarm">闹钟</TabsTrigger>
                <TabsTrigger value="csi">CSI</TabsTrigger>
                <TabsTrigger value="voice">语音</TabsTrigger>
                <TabsTrigger value="settings">设置</TabsTrigger>
              </TabsList>
            </Tabs>
          </div>

          {/* Content Area */}
          <main className="flex-1 min-w-0">
            {activeTab === 'dashboard' && (
              <DashboardTab
                devices={devices}
                alarms={alarms}
                csiThreshold={csiThreshold}
                voiceLogs={voiceLogs}
                isNightMode={isNightMode}
                currentTime={currentTime}
                nextAlarm={nextAlarm}
              />
            )}
            {activeTab === 'light' && (
              <LightTab
                isOn={isLightOn}
                setIsOn={setIsLightOn}
                brightness={ledBrightness}
                setBrightness={setLedBrightness}
              />
            )}
            {activeTab === 'alarm' && (
              <AlarmTab alarms={alarms} addAlarm={addAlarm} updateAlarm={updateAlarm} deleteAlarm={deleteAlarm} />
            )}
            {activeTab === 'csi' && <CSITab csiThreshold={csiThreshold} setCsiThreshold={setCsiThreshold} csiHistory={csiHistory} />}
            {activeTab === 'voice' && <VoiceTab voiceLogs={voiceLogs} />}
            {activeTab === 'settings' && <SettingsTab devices={devices} />}
          </main>
        </div>
      </div>
    </div>
  )
}

// ============== SIDEBAR BUTTON ==============
function SidebarButton({ icon, label, active, onClick }: { icon: React.ReactNode; label: string; active: boolean; onClick: () => void }) {
  return (
    <button
      onClick={onClick}
      className={`w-full flex items-center gap-3 px-4 py-3 rounded-xl text-sm font-medium transition-all duration-200 ${
        active ? 'bg-orange-50 text-orange-700 shadow-sm' : 'text-stone-600 hover:bg-stone-100 hover:text-stone-900'
      }`}
    >
      {icon}
      {label}
    </button>
  )
}

// ============== DASHBOARD TAB ==============
function DashboardTab({
  devices,
  csiThreshold,
  isNightMode,
  currentTime,
  nextAlarm,
}: {
  devices: Device[]
  alarms: Alarm[]
  csiThreshold: number
  voiceLogs: VoiceLog[]
  isNightMode: boolean
  currentTime: string
  nextAlarm?: Alarm
}) {
  return (
    <div className="space-y-6">
      {/* Hero Card */}
      <div className="bg-gradient-to-br from-orange-500 to-orange-700 rounded-3xl p-8 text-white shadow-lg">
        <div className="flex items-start justify-between">
          <div>
            <p className="text-orange-100 text-sm mb-1">当前时间</p>
            <h2 className="text-5xl font-bold tracking-tight">{currentTime}</h2>
            <p className="text-orange-100 mt-2">{isNightMode ? '夜深了，祝您好梦' : '早上好，开启美好的一天'}</p>
          </div>
          <div className="w-16 h-16 rounded-2xl bg-white/20 backdrop-blur flex items-center justify-center">
            {isNightMode ? <Moon size={28} /> : <Sun size={28} />}
          </div>
        </div>

        <div className="mt-8 grid grid-cols-3 gap-4">
          <div className="bg-white/10 backdrop-blur rounded-2xl p-4">
            <p className="text-orange-100 text-xs mb-1">下次闹钟</p>
            <p className="text-xl font-semibold">
              {nextAlarm ? `${String(nextAlarm.hour).padStart(2, '0')}:${String(nextAlarm.minute).padStart(2, '0')}` : '未设置'}
            </p>
            {nextAlarm && <p className="text-orange-100 text-xs mt-1">{nextAlarm.label}</p>}
          </div>
          <div className="bg-white/10 backdrop-blur rounded-2xl p-4">
            <p className="text-orange-100 text-xs mb-1">人体检测</p>
            <p className="text-xl font-semibold">检测中</p>
            <p className="text-orange-100 text-xs mt-1">阈值: {csiThreshold}</p>
          </div>
          <div className="bg-white/10 backdrop-blur rounded-2xl p-4">
            <p className="text-orange-100 text-xs mb-1">设备状态</p>
            <p className="text-xl font-semibold">
              {devices.filter((d) => d.is_online).length} / {devices.length}
            </p>
            <p className="text-orange-100 text-xs mt-1">在线设备</p>
          </div>
        </div>
      </div>

      {/* Quick Actions */}
      <div className="grid grid-cols-3 gap-4">
        <QuickActionCard icon={<Lightbulb size={24} />} title="夜灯开关" desc="控制床头夜灯" color="from-amber-400 to-amber-600" />
        <QuickActionCard icon={<Bell size={24} />} title="贪睡模式" desc="延时5分钟提醒" color="from-blue-400 to-blue-600" />
        <QuickActionCard icon={<Mic size={24} />} title="语音助手" desc="唤醒语音助手" color="from-purple-400 to-purple-600" />
      </div>

      {/* Recent Logs */}
      <Card>
        <CardHeader>
          <CardTitle className="text-lg">最近对话</CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {voiceLogs.slice(-3).map((log, i) => (
              <div key={i} className={`flex gap-3 ${log.type === 'user' ? 'flex-row-reverse' : ''}`}>
                <div
                  className={`w-8 h-8 rounded-full flex items-center justify-center flex-shrink-0 ${
                    log.type === 'user' ? 'bg-orange-100 text-orange-700' : 'bg-stone-100 text-stone-600'
                  }`}
                >
                  {log.type === 'user' ? <User size={14} /> : <Mic size={14} />}
                </div>
                <div
                  className={`max-w-md px-4 py-2 rounded-2xl text-sm ${
                    log.type === 'user' ? 'bg-orange-500 text-white rounded-tr-sm' : 'bg-stone-100 text-stone-700 rounded-tl-sm'
                  }`}
                >
                  {log.text}
                </div>
                <span className="text-xs text-stone-400 self-end">{log.time}</span>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>
    </div>
  )
}

function QuickActionCard({ icon, title, desc, color }: { icon: React.ReactNode; title: string; desc: string; color: string }) {
  return (
    <button className="bg-white rounded-2xl shadow-sm p-5 hover:shadow-md transition-shadow text-left group">
      <div className={`w-12 h-12 rounded-xl bg-gradient-to-br ${color} flex items-center justify-center text-white mb-3 group-hover:scale-110 transition-transform`}>
        {icon}
      </div>
      <h4 className="font-semibold text-stone-900">{title}</h4>
      <p className="text-sm text-stone-500 mt-1">{desc}</p>
    </button>
  )
}

// ============== LIGHT TAB ==============
function LightTab({
  isOn,
  setIsOn,
  brightness,
  setBrightness,
}: {
  isOn: boolean
  setIsOn: (v: boolean) => void
  brightness: number[]
  setBrightness: (v: number[]) => void
}) {
  const presets = [
    { label: '夜灯', value: 10 },
    { label: '柔和', value: 30 },
    { label: '阅读', value: 60 },
    { label: '明亮', value: 100 },
  ]

  return (
    <div className="space-y-6">
      <Card>
        <CardHeader>
          <CardTitle className="text-xl">灯光控制</CardTitle>
        </CardHeader>
        <CardContent className="space-y-6">
          {/* Light toggle */}
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-4">
              <div
                className={`w-16 h-16 rounded-2xl flex items-center justify-center transition-all ${
                  isOn ? 'bg-amber-400 shadow-lg shadow-amber-200' : 'bg-stone-100'
                }`}
              >
                <Lightbulb size={28} className={isOn ? 'text-white' : 'text-stone-400'} />
              </div>
              <div>
                <p className="font-medium text-stone-900">{isOn ? '灯光已开启' : '灯光已关闭'}</p>
                <p className="text-sm text-stone-500">{brightness[0]}% 亮度</p>
              </div>
            </div>
            <Switch checked={isOn} onCheckedChange={setIsOn} />
          </div>

          {isOn && (
            <div className="space-y-4 animate-in fade-in slide-in-from-top-2">
              <div>
                <Label className="text-sm font-medium text-stone-700 mb-2 block">亮度调节</Label>
                <Slider value={brightness} onValueChange={setBrightness} max={100} step={1} className="w-full" />
                <div className="flex justify-between text-xs text-stone-400 mt-1">
                  <span>暗</span>
                  <span>亮</span>
                </div>
              </div>

              <div>
                <Label className="text-sm font-medium text-stone-700 mb-2 block">快速预设</Label>
                <div className="flex gap-2">
                  {presets.map((preset) => (
                    <Button
                      key={preset.label}
                      variant={brightness[0] === preset.value ? 'default' : 'outline'}
                      size="sm"
                      onClick={() => setBrightness([preset.value])}
                      className={brightness[0] === preset.value ? 'bg-orange-500 hover:bg-orange-600' : ''}
                    >
                      {preset.label}
                    </Button>
                  ))}
                </div>
              </div>
            </div>
          )}

          <Separator />

          {/* Adaptive settings */}
          <div>
            <h3 className="text-sm font-semibold text-stone-700 mb-4">智能控制</h3>
            <div className="space-y-3">
              <ToggleSetting label="人体感应自动亮灯" defaultChecked />
              <ToggleSetting label="离床自动关灯" defaultChecked />
              <ToggleSetting label="夜间模式 (22:00-06:00)" defaultChecked />
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  )
}

function ToggleSetting({ label, defaultChecked }: { label: string; defaultChecked?: boolean }) {
  return (
    <div className="flex items-center justify-between py-2">
      <span className="text-sm text-stone-700">{label}</span>
      <Switch defaultChecked={defaultChecked} />
    </div>
  )
}

// ============== ALARM TAB ==============
function AlarmTab({
  alarms,
  addAlarm,
  updateAlarm,
  deleteAlarm,
}: {
  alarms: Alarm[]
  addAlarm: () => void
  updateAlarm: (index: number, updates: Partial<Alarm>) => void
  deleteAlarm: (index: number) => void
}) {
  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold text-stone-900">闹钟管理</h2>
        <Button onClick={addAlarm} className="bg-orange-500 hover:bg-orange-600">
          <Plus size={16} className="mr-1" />
          添加闹钟
        </Button>
      </div>

      <div className="space-y-3">
        {alarms.map((alarm) => (
          <Card key={alarm.index}>
            <CardContent className="p-5">
              <div className="flex items-center justify-between">
                <div className="flex items-center gap-4">
                  <Switch
                    checked={alarm.enabled}
                    onCheckedChange={(checked) => updateAlarm(alarm.index, { enabled: checked })}
                  />
                  <div>
                    <div className="flex items-baseline gap-1">
                      <span className={`text-3xl font-bold ${alarm.enabled ? 'text-stone-900' : 'text-stone-400'}`}>
                        {String(alarm.hour).padStart(2, '0')}:{String(alarm.minute).padStart(2, '0')}
                      </span>
                    </div>
                    <p className="text-sm text-stone-500">{alarm.label}</p>
                  </div>
                </div>

                <div className="flex items-center gap-3">
                  <Badge variant="secondary">{formatDays(alarm.days)}</Badge>
                  <Button variant="ghost" size="sm" onClick={() => deleteAlarm(alarm.index)} className="text-stone-400 hover:text-red-500">
                    <Trash2 size={16} />
                  </Button>
                </div>
              </div>

              {alarm.enabled && (
                <div className="mt-4 pt-4 border-t border-stone-100 grid grid-cols-3 gap-4">
                  <div>
                    <Label className="text-xs text-stone-500 block mb-1">小时</Label>
                    <input
                      type="number"
                      min={0}
                      max={23}
                      value={alarm.hour}
                      onChange={(e) => updateAlarm(alarm.index, { hour: Number(e.target.value) })}
                      className="w-full px-3 py-2 border border-stone-200 rounded-lg text-sm"
                    />
                  </div>
                  <div>
                    <Label className="text-xs text-stone-500 block mb-1">分钟</Label>
                    <input
                      type="number"
                      min={0}
                      max={59}
                      value={alarm.minute}
                      onChange={(e) => updateAlarm(alarm.index, { minute: Number(e.target.value) })}
                      className="w-full px-3 py-2 border border-stone-200 rounded-lg text-sm"
                    />
                  </div>
                  <div>
                    <Label className="text-xs text-stone-500 block mb-1">标签</Label>
                    <input
                      type="text"
                      value={alarm.label}
                      onChange={(e) => updateAlarm(alarm.index, { label: e.target.value })}
                      className="w-full px-3 py-2 border border-stone-200 rounded-lg text-sm"
                    />
                  </div>
                </div>
              )}
            </CardContent>
          </Card>
        ))}
      </div>

      <Card>
        <CardHeader>
          <CardTitle className="text-lg">智能唤醒设置</CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          <ToggleSetting label="渐进式亮度唤醒" defaultChecked />
          <ToggleSetting label="检测起身自动停止闹钟" defaultChecked />
          <ToggleSetting label="贪睡语音协商" defaultChecked />
          <div>
            <Label className="text-sm font-medium text-stone-700 mb-2 block">唤醒时长 (分钟)</Label>
            <Slider defaultValue={[10]} max={30} min={5} step={1} />
          </div>
        </CardContent>
      </Card>
    </div>
  )
}

// ============== CSI TAB ==============
function CSITab({
  csiThreshold,
  setCsiThreshold,
  csiHistory,
}: {
  csiThreshold: number
  setCsiThreshold: (v: number) => void
  csiHistory: CSIPoint[]
}) {
  return (
    <div className="space-y-6">
      <Card>
        <CardHeader>
          <CardTitle className="text-xl">CSI 人体检测</CardTitle>
        </CardHeader>
        <CardContent className="space-y-6">
          {/* Status Cards */}
          <div className="grid grid-cols-4 gap-4">
            <StatusCard title="人体存在" value={csiHistory.length > 0 && csiHistory[csiHistory.length - 1].presence > 0 ? "检测到" : "未检测"} status={csiHistory.length > 0 && csiHistory[csiHistory.length - 1].presence > 0 ? "success" : "neutral"} />
            <StatusCard title="数据点" value={String(csiHistory.length)} status="info" />
            <StatusCard title="当前方差" value={csiHistory.length > 0 ? String(csiHistory[csiHistory.length - 1].variance.toFixed(1)) : "-"} status="info" />
            <StatusCard title="阈值" value={String(csiThreshold)} status="warning" />
          </div>

          {/* CSI Chart */}
          <div className="mt-6">
            <h3 className="text-sm font-semibold text-stone-700 mb-3">CSI 信号变化</h3>
            <div className="relative h-48 bg-stone-50 rounded-xl overflow-hidden">
              {csiHistory.length > 0 ? (
                <CSIChart data={csiHistory} threshold={csiThreshold} />
              ) : (
                <div className="flex items-center justify-center h-full text-stone-400 text-sm">暂无 CSI 数据</div>
              )}
            </div>
          </div>

          <Separator />

          {/* Threshold Control */}
          <div>
            <h3 className="text-sm font-semibold text-stone-700 mb-4">检测阈值设置</h3>
            <div>
              <Label className="text-sm text-stone-600 mb-2 block">人体存在阈值: {csiThreshold}</Label>
              <Slider value={[csiThreshold]} onValueChange={(v) => setCsiThreshold(v[0])} max={50} min={5} step={0.5} />
              <div className="flex justify-between text-xs text-stone-400 mt-1">
                <span>灵敏 (5)</span>
                <span>适中 (25)</span>
                <span>保守 (50)</span>
              </div>
            </div>

            <div className="mt-4 bg-amber-50 border border-amber-200 rounded-xl p-4">
              <p className="text-sm text-amber-800">
                <strong>提示:</strong> 阈值越低，检测越灵敏但可能产生误报。建议根据实际环境调整，通常在10-20之间。
              </p>
            </div>
          </div>

          <Separator />

          {/* Calibration */}
          <div>
            <h3 className="text-sm font-semibold text-stone-700 mb-4">校准</h3>
            <div className="flex gap-3">
              <Button className="bg-orange-500 hover:bg-orange-600">开始空房间校准</Button>
              <Button variant="outline">重置为默认值</Button>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Detection Log */}
      <Card>
        <CardHeader>
          <CardTitle className="text-lg">检测日志</CardTitle>
        </CardHeader>
        <CardContent>
          {csiHistory.length > 0 ? (
            <div className="space-y-2">
              {csiHistory.slice(-5).reverse().map((log, i) => (
                <div key={i} className="flex items-center gap-4 py-2 px-3 rounded-lg hover:bg-stone-50">
                  <span className="text-xs text-stone-400 w-20">{new Date(log.time * 1000).toLocaleTimeString('zh-CN')}</span>
                  <span className="text-sm text-stone-700 flex-1">{log.presence > 0 ? '检测到人体' : '未检测到人体'}</span>
                  <span className="text-xs text-stone-500">方差: {log.variance.toFixed(1)}</span>
                  <Badge variant="outline" className={log.presence > 0 ? "text-green-600 border-green-200 bg-green-50" : "text-stone-500 border-stone-200 bg-stone-50"}>
                    {log.presence > 0 ? '有' : '无'}
                  </Badge>
                </div>
              ))}
            </div>
          ) : (
            <div className="text-sm text-stone-400 py-4">暂无检测日志</div>
          )}
        </CardContent>
      </Card>
    </div>
  )
}

function StatusCard({ title, value, status }: { title: string; value: string; status: string }) {
  const colors: Record<string, string> = {
    success: 'bg-green-50 text-green-700 border-green-200',
    warning: 'bg-amber-50 text-amber-700 border-amber-200',
    info: 'bg-blue-50 text-blue-700 border-blue-200',
    neutral: 'bg-stone-50 text-stone-600 border-stone-200',
  }

  return (
    <div className={`border rounded-xl p-4 ${colors[status] || colors.neutral}`}>
      <p className="text-xs opacity-70 mb-1">{title}</p>
      <p className="text-lg font-semibold">{value}</p>
    </div>
  )
}

// ============== VOICE TAB ==============
function VoiceTab({ voiceLogs }: { voiceLogs: VoiceLog[] }) {
  return (
    <div className="space-y-6">
      <Card>
        <CardHeader>
          <CardTitle className="text-xl">语音对话记录</CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-4">
            {voiceLogs.map((log, i) => (
              <div key={i} className="flex gap-4 p-4 rounded-xl bg-stone-50">
                <div
                  className={`w-10 h-10 rounded-full flex items-center justify-center flex-shrink-0 ${
                    log.type === 'user' ? 'bg-orange-100 text-orange-700' : 'bg-blue-100 text-blue-700'
                  }`}
                >
                  {log.type === 'user' ? <User size={18} /> : <Mic size={18} />}
                </div>
                <div className="flex-1">
                  <div className="flex items-center gap-2 mb-1">
                    <span className="text-sm font-medium text-stone-900">{log.type === 'user' ? '我' : 'AI助手'}</span>
                    <span className="text-xs text-stone-400">{log.time}</span>
                  </div>
                  <p className="text-sm text-stone-700">{log.text}</p>
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle className="text-lg">语音设置</CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          <div>
            <Label className="text-sm font-medium text-stone-700 mb-2 block">唤醒词</Label>
            <input type="text" defaultValue="Hi Fairy" className="w-full px-4 py-2 border border-stone-200 rounded-xl text-sm" />
          </div>
          <div>
            <Label className="text-sm font-medium text-stone-700 mb-2 block">TTS 语音</Label>
            <select className="w-full px-4 py-2 border border-stone-200 rounded-xl text-sm bg-white">
              <option>晓晓 (中文女声)</option>
              <option>云希 (中文男声)</option>
              <option>Aria (English Female)</option>
            </select>
          </div>
          <ToggleSetting label="离线唤醒词识别" defaultChecked />
          <ToggleSetting label="语音唤醒反馈音" defaultChecked />
        </CardContent>
      </Card>
    </div>
  )
}

// ============== SETTINGS TAB ==============
function SettingsTab({ devices }: { devices: Device[] }) {
  return (
    <div className="space-y-6">
      <Card>
        <CardHeader>
          <CardTitle className="text-xl">设备设置</CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          {devices.map((device) => (
            <div key={device.device_id} className="p-4 bg-stone-50 rounded-xl">
              <div className="flex items-center justify-between mb-3">
                <div>
                  <p className="font-medium text-stone-900">{device.name}</p>
                  <p className="text-xs text-stone-500">{device.device_id}</p>
                </div>
                <Badge variant={device.is_online ? 'default' : 'destructive'} className={device.is_online ? 'bg-green-100 text-green-700 hover:bg-green-100' : ''}>
                  {device.is_online ? '在线' : '离线'}
                </Badge>
              </div>
              <div className="grid grid-cols-2 gap-4 text-sm">
                <div>
                  <span className="text-stone-500">固件版本: </span>
                  <span className="text-stone-700">{device.firmware_version}</span>
                </div>
                <div>
                  <span className="text-stone-500">WiFi信号: </span>
                  <span className="text-stone-700">{device.wifi_rssi} dBm</span>
                </div>
              </div>
            </div>
          ))}

          <Separator />

          <div>
            <h3 className="text-lg font-semibold text-stone-900 mb-4">系统设置</h3>
            <div className="space-y-4">
              <div>
                <Label className="text-sm font-medium text-stone-700 mb-2 block">服务器地址</Label>
                <input type="text" defaultValue={window.location.host} className="w-full px-4 py-2 border border-stone-200 rounded-xl text-sm" />
              </div>
              <div>
                <Label className="text-sm font-medium text-stone-700 mb-2 block">设备API密钥</Label>
                <input type="password" defaultValue="••••••••••••" className="w-full px-4 py-2 border border-stone-200 rounded-xl text-sm" />
              </div>
              <ToggleSetting label="自动更新固件" defaultChecked />
              <ToggleSetting label="调试模式" />
            </div>
          </div>

          <Separator />

          <div>
            <h3 className="text-lg font-semibold text-red-600 mb-4">危险区域</h3>
            <div className="flex gap-3">
              <Button variant="outline" className="text-red-600 border-red-200 hover:bg-red-50">
                <Power size={16} className="mr-1" />
                重启设备
              </Button>
              <Button variant="outline" className="text-red-600 border-red-200 hover:bg-red-50">
                恢复出厂设置
              </Button>
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  )
}

export default App
