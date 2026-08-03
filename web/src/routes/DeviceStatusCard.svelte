<script>
  import { onMount } from "svelte";
  import { listenForDeviceStatus } from "$lib/Firebase.js";

  const MAX_STALE_MS = 5 * 60 * 1000;

  let deviceStatus = null;
  let unsubscribeDeviceStatus;
  let isOnline = false;

  function formatUptime(seconds) {
    const totalSeconds = Number(seconds || 0);
    const minutes = Math.floor(totalSeconds / 60);
    const hours = Math.floor(minutes / 60);

    if (hours > 0) {
      return `${hours} h ${minutes % 60} min`;
    }

    if (minutes > 0) {
      return `${minutes} min`;
    }

    return `${totalSeconds} s`;
  }

  function formatTimestamp(value) {
    if (!value) return "–";
    const date = new Date(Number(value) * 1000);
    return date.toLocaleString();
  }

  function isFreshStatus(status) {
    const timestamp = Number(status?.lastSeen ?? status?.ts ?? 0);
    if (!timestamp) return false;

    const lastEventTime = timestamp * 1000;
    return Date.now() - lastEventTime <= MAX_STALE_MS;
  }

  $: isOnline = Boolean(deviceStatus && isFreshStatus(deviceStatus) && (deviceStatus.online ?? true));

  onMount(() => {
    unsubscribeDeviceStatus = listenForDeviceStatus((status) => {
      deviceStatus = status;
    });

    const interval = setInterval(() => {
      isOnline = Boolean(deviceStatus && isFreshStatus(deviceStatus) && (deviceStatus.online ?? true));
    }, 10_000);

    return () => {
      clearInterval(interval);
      if (unsubscribeDeviceStatus) {
        unsubscribeDeviceStatus();
      }
    };
  });
</script>

{#if deviceStatus}
  <div class=" p-4 mx-auto mt-4 text-sm border rounded-lg shadow-sm bg-base-100 border-base-300">
    <div class="flex items-center justify-between">
      <p class="font-semibold">Enhetsstatus</p>
      <span class="badge {isOnline ? 'badge-success' : 'badge-error'}">{isOnline ? 'Online' : 'Offline'}</span>
    </div>
    <div class="mt-2 space-y-1 text-gray-600">
      <p>Uptime: {formatUptime(deviceStatus.uptime)}</p>
      <p>Heap: {deviceStatus.freeHeap ?? '–'} bytes</p>
      <p>Min heap: {deviceStatus.minHeap ?? '–'} bytes</p>
      <p>Fragmentering: {deviceStatus.frag ?? '–'}%</p>
      <p>Signal: {deviceStatus.rssi ?? '–'} dBm</p>
      <p>Senast sedd: {formatTimestamp(deviceStatus.lastSeen ?? deviceStatus.ts)}</p>
    </div>
  </div>
{/if}
