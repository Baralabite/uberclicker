function previewAction(action) {
  document.querySelector('#last-action').textContent = `Last: ${action}`;
  document.querySelector('#last-receipt').textContent = 'Acknowledged · simulated';
  document.querySelector('#action-feedback').textContent = `${action} action. Acknowledgement simulated for this preview.`;
}
document.querySelector('#previous-cue').addEventListener('click', () => previewAction('Previous'));
document.querySelector('#next-cue').addEventListener('click', () => previewAction('Next'));
const connectionDetails = {
  ble: 'Pair directly over BLE in the native app. Configuration pauses cue output until you apply or cancel your changes.',
  lan: 'Use the native app on the same network when your device is connected to Wi-Fi. Configuration pauses cue output until you apply or cancel.',
  linked: 'Future option: remote access needs an authenticated cloud relay and an online device. Linking an account alone does not make a device reachable.'
};
document.querySelectorAll('input[name="connection"]').forEach(input => {
  input.addEventListener('change', () => {
    document.querySelector('#connection-detail').textContent = connectionDetails[input.value];
  });
});
// The informative illustration remains available if modules cannot load.
setTimeout(() => {
  const stage = document.querySelector('#device-viewport');
  if (!stage.classList.contains('loaded') && document.querySelector('#model-status').textContent === 'Loading 3D preview…') {
    document.querySelector('#model-status').textContent = 'Static illustration · 3D unavailable';
  }
}, 7000);
