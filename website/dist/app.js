const buyCountry = document.querySelector('#buy-country');
function updateBuyLinks() {
  document.querySelectorAll('#buy .buy-options').forEach(list => {
    list.hidden = list.id !== buyCountry.value;
  });
}
buyCountry.addEventListener('change', updateBuyLinks);
updateBuyLinks();
