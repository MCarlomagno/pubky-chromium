for (const button of document.querySelectorAll('.key-address button')) {
  button.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(button.previousElementSibling.href);
      button.textContent = 'Copied';
    } catch {
      button.textContent = 'Copy failed';
    }
  });
}
