import { test, expect } from '@playwright/test';

test('sidebar toggle works', async ({ page }) => {
  await page.goto('http://localhost:8081');
  
  const sidebar = page.locator('#sidebar');
  const toggle = page.locator('form[action="/toggle_sidebar"] button');
  
  // Initial state (Desktop default: visible)
  const initialBox = await sidebar.boundingBox();
  expect(initialBox).not.toBeNull();
  const initialX = initialBox!.x;
  
  // Toggle closed
  await toggle.click();
  await page.waitForTimeout(500); // Wait for transition
  
  const closedBox = await sidebar.boundingBox();
  expect(closedBox!.x).toBeLessThan(initialX);
  
  // Toggle open
  await toggle.click();
  await page.waitForTimeout(500);
  
  const openBox = await sidebar.boundingBox();
  expect(openBox!.x).toBe(initialX);
});

test('theme toggle works', async ({ page }) => {
  await page.goto('http://localhost:8081');
  
  const body = page.locator('body');
  const toggle = page.locator('form[action="/toggle_theme"] button');
  
  const initialBg = await body.evaluate((el) => getComputedStyle(el).backgroundColor);
  
  await toggle.click();
  await page.waitForTimeout(100); // No transition usually
  
  const toggledBg = await body.evaluate((el) => getComputedStyle(el).backgroundColor);
  expect(toggledBg).not.toBe(initialBg);
  
  await toggle.click();
  const restoredBg = await body.evaluate((el) => getComputedStyle(el).backgroundColor);
  expect(restoredBg).toBe(initialBg);
});
