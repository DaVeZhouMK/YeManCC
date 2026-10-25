import './memory-ports';
import '@/gamepad/engine';
import{createApp}from'vue';import{createPinia}from'pinia';import SettingsView from './target.vue';
import{CURRENT_DIAGNOSTIC_LOG_FILES}from'@/bridge/diagnosticLogMaintenance';
import{loadUiSettings}from'@/bridge/uiSettings';
const w=window as any;w.__fixture.currentLogFiles=[...CURRENT_DIAGNOSTIC_LOG_FILES];w.__previewErrors=[];w.__navigate=()=>{};w.__currentAuditRoute=()=>'/settings';
async function mount(){await loadUiSettings();const app=createApp(SettingsView);app.use(createPinia());app.config.errorHandler=(error:any,_instance,info)=>w.__previewErrors.push({message:String(error),stack:error?.stack,info});app.mount('#app');w.__unmount=()=>app.unmount();w.__settingsMounted=true;}
mount().catch(error=>w.__previewErrors.push({message:String(error),stack:error?.stack}));
