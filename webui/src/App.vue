<script setup lang="ts">
import { ref } from 'vue'
import TopBar from './components/TopBar.vue'
import WaterfallSpectrum from './components/WaterfallSpectrum.vue'
import SidebarDrawer from './components/SidebarDrawer.vue'
import MiniProtocolHud from './components/MiniProtocolHud.vue'
import FullProtocolWorkbench from './components/FullProtocolWorkbench.vue'
import PacketInspector from './components/PacketInspector.vue'

const currentView = ref<'rf' | 'protocol' | 'split'>('rf')

function setView(view: 'rf' | 'protocol' | 'split') {
  currentView.value = view
}
</script>

<template>
  <div class="w-screen h-screen flex flex-col bg-sdr-dark text-slate-200 overflow-hidden font-sans select-none">
    <!-- Top Global Header Control Bar -->
    <TopBar :active-view="currentView" @change-view="setView" />

    <!-- 1. Primary RF Studio View: Fullscreen Waterfall + Floating Drawer + Mini Protocol HUD -->
    <div v-if="currentView === 'rf'" class="flex-1 relative flex min-h-0 overflow-hidden">
      <!-- Left Floating Sidebar Drawer -->
      <SidebarDrawer />

      <!-- Fullscreen High-Performance Waterfall & Spectrum Canvas -->
      <div class="flex-1 h-full p-2">
        <WaterfallSpectrum />
      </div>

      <!-- Right Floating Mini Protocol HUD -->
      <MiniProtocolHud @open-full-workbench="setView('protocol')" />
    </div>

    <!-- 2. Fullscreen Protocol Workbench View: Deep Frame Analysis -->
    <div v-else-if="currentView === 'protocol'" class="flex-1 min-h-0">
      <FullProtocolWorkbench @close="setView('rf')" />
    </div>

    <!-- 3. Split Screen View: Top Waterfall (60%) + Bottom Packet Stream (40%) -->
    <div v-else class="flex-1 flex min-h-0 p-2 gap-2 overflow-hidden">
      <!-- Left Floating Sidebar Drawer -->
      <SidebarDrawer />

      <main class="flex-1 flex flex-col gap-2 min-w-0">
        <!-- Waterfall & Spectrum (Top 55%) -->
        <div class="flex-[0.55] min-h-[220px]">
          <WaterfallSpectrum />
        </div>

        <!-- Packet Inspector (Bottom 45%) -->
        <div class="flex-[0.45] min-h-[180px]">
          <PacketInspector />
        </div>
      </main>
    </div>
  </div>
</template>
