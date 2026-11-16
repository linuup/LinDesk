const todoList = document.getElementById('todoList');
const todoInput = document.getElementById('todoInput');
const addBtn = document.getElementById('addBtn');
const themeToggle = document.getElementById('themeToggle');
const settingsBtn = document.getElementById('settingsBtn');

if (settingsBtn) {
  settingsBtn.addEventListener('click', () => {
    if (window.desktop && desktop.openSettings) {
      desktop.openSettings();
    }
  });
}

let tasks = JSON.parse(localStorage.getItem('lindesk_todo_tasks')) || [];

function renderTasks() {
  todoList.innerHTML = '';
  tasks.forEach((task, index) => {
    const li = document.createElement('li');
    li.className = `todo-item ${task.completed ? 'completed' : ''}`;
    
    const checkbox = document.createElement('input');
    checkbox.type = 'checkbox';
    checkbox.className = 'todo-checkbox';
    checkbox.checked = task.completed;
    checkbox.addEventListener('change', () => toggleTask(index));
    
    const span = document.createElement('span');
    span.className = 'todo-text';
    span.textContent = task.text;
    
    const delBtn = document.createElement('button');
    delBtn.className = 'delete-btn';
    delBtn.title = 'Delete';
    delBtn.innerHTML = `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>`;
    delBtn.addEventListener('click', () => deleteTask(index));
    
    li.appendChild(checkbox);
    li.appendChild(span);
    li.appendChild(delBtn);
    todoList.appendChild(li);
  });
}

function addTask(text) {
  if (!text.trim()) return;
  tasks.push({ text: text.trim(), completed: false });
  saveTasks();
  renderTasks();
  todoInput.value = '';
  
  // Scroll to bottom
  const main = document.querySelector('.todo-main');
  main.scrollTop = main.scrollHeight;
}

function toggleTask(index) {
  tasks[index].completed = !tasks[index].completed;
  saveTasks();
  renderTasks();
}

function deleteTask(index) {
  tasks.splice(index, 1);
  saveTasks();
  renderTasks();
}

function saveTasks() {
  localStorage.setItem('lindesk_todo_tasks', JSON.stringify(tasks));
}

addBtn.addEventListener('click', () => addTask(todoInput.value));
todoInput.addEventListener('keypress', (e) => {
  if (e.key === 'Enter') addTask(todoInput.value);
});

// Theme Logic
function bindThemeToggle() {
  const currentTheme = localStorage.getItem('lindesk_todo_theme') || 'dark';

  if (currentTheme === 'light') {
    document.body.classList.add('light-theme');
    themeToggle.textContent = '🌙';
  } else {
    themeToggle.textContent = '☀️';
  }

  themeToggle.addEventListener('click', () => {
    document.body.classList.toggle('light-theme');
    const isLight = document.body.classList.contains('light-theme');
    localStorage.setItem('lindesk_todo_theme', isLight ? 'light' : 'dark');
    themeToggle.textContent = isLight ? '🌙' : '☀️';
  });
}

function loadSettings() {
  const customTitle = localStorage.getItem('lindesk_todo_title');
  if (customTitle) {
    document.querySelector('.widget-title').textContent = customTitle;
  }
  
  const customAccent = localStorage.getItem('lindesk_todo_accent');
  if (customAccent) {
    document.documentElement.style.setProperty('--accent', customAccent);
  }
}

bindThemeToggle();
loadSettings();
renderTasks();

// Synchronize seamlessly when settings dialog is used
window.addEventListener('storage', (e) => {
  if (e.key === 'lindesk_todo_title' || e.key === 'lindesk_todo_accent') {
    loadSettings();
  } else if (e.key === 'lindesk_todo_tasks') {
    tasks = JSON.parse(e.newValue) || [];
    renderTasks();
  }
});
