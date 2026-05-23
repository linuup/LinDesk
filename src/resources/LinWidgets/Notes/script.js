const noteArea = document.getElementById('noteArea');

// Load saved notes
const saved = localStorage.getItem('lin_sticky_notes');
if (saved) {
  noteArea.value = saved;
}

// Save on input with debounce
let timeout;
noteArea.addEventListener('input', () => {
  clearTimeout(timeout);
  timeout = setTimeout(() => {
    localStorage.setItem('lin_sticky_notes', noteArea.value);
  }, 500); // 500ms debounce
});
