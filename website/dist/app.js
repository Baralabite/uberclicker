const buyCountry = document.querySelector('#buy-country');
function updateBuyLinks() {
  document.querySelectorAll('#buy .buy-options').forEach(list => {
    list.hidden = list.id !== buyCountry.value;
  });
}
buyCountry.addEventListener('change', updateBuyLinks);
updateBuyLinks();

// The informative illustration remains available if modules cannot load.
setTimeout(() => {
  const stage = document.querySelector('#device-viewport');
  if (!stage.classList.contains('loaded') && document.querySelector('#model-status').textContent === 'Loading 3D preview…') {
    document.querySelector('#model-status').textContent = 'Static illustration · 3D unavailable';
  }
}, 7000);
