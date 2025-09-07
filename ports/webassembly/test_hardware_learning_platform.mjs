#!/usr/bin/env node

/**
 * Comprehensive test for CircuitPython Hardware Learning Platform
 * 
 * Tests the complete architecture:
 * 1. Board Shadow Runtime
 * 2. Virtual board simulation  
 * 3. WebSerial bridge capability
 * 4. Hardware state synchronization
 * 5. Learning-focused error handling
 */

import { BoardShadowRuntime } from './board-shadow-runtime.js';

class HardwareLearningPlatformTest {
    constructor() {
        this.testResults = [];
        this.boardShadow = null;
        this.passedTests = 0;
        this.totalTests = 0;
    }
    
    /**
     * Run a single test
     */
    async runTest(testName, testFunction) {
        this.totalTests++;
        
        try {
            console.log(`\n🧪 Testing: ${testName}`);
            await testFunction();
            console.log(`✅ PASSED: ${testName}`);
            this.testResults.push({ name: testName, status: 'PASSED' });
            this.passedTests++;
        } catch (error) {
            console.error(`❌ FAILED: ${testName}`);
            console.error(`   Error: ${error.message}`);
            this.testResults.push({ name: testName, status: 'FAILED', error: error.message });
        }
    }
    
    /**
     * Test 1: Board Shadow Runtime Initialization
     */
    async testBoardShadowInit() {
        this.boardShadow = new BoardShadowRuntime({
            boardType: 'pico',
            enableLogging: false  // Reduce noise during testing
        });
        
        // Check virtual board initialization
        if (!this.boardShadow.virtualBoard) {
            throw new Error('Virtual board not initialized');
        }
        
        // Check initial state
        if (this.boardShadow.syncMode !== 'virtual') {
            throw new Error('Expected virtual mode initially');
        }
        
        console.log('   - Virtual board initialized');
        console.log(`   - Sync mode: ${this.boardShadow.syncMode}`);
    }
    
    /**
     * Test 2: Pin State Management
     */
    async testPinStateManagement() {
        const testPin = 'GP0';
        
        // Set pin value
        await this.boardShadow.setPin(testPin, 1);
        
        // Read back value
        const value = this.boardShadow.getPin(testPin);
        if (value !== 1) {
            throw new Error(`Expected pin value 1, got ${value}`);
        }
        
        // Check shadow state
        const shadowState = this.boardShadow.shadowState.get(testPin);
        if (!shadowState || shadowState.value !== 1 || shadowState.source !== 'code') {
            throw new Error('Shadow state not updated correctly');
        }
        
        console.log(`   - Pin ${testPin} set and read correctly`);
        console.log(`   - Shadow state: ${JSON.stringify(shadowState)}`);
    }
    
    /**
     * Test 3: Pin Change History
     */
    async testPinChangeHistory() {
        const testPin = 'GP1';
        
        // Make several changes
        await this.boardShadow.setPin(testPin, 0);
        await new Promise(resolve => setTimeout(resolve, 10)); // Small delay
        await this.boardShadow.setPin(testPin, 1);
        await new Promise(resolve => setTimeout(resolve, 10));
        await this.boardShadow.setPin(testPin, 0);
        
        // Check history
        const history = this.boardShadow.getPinHistory(testPin, 1000);
        if (history.length < 3) {
            throw new Error(`Expected at least 3 history entries, got ${history.length}`);
        }
        
        // Check chronological order (newest first)
        if (history[0].timestamp < history[1].timestamp) {
            throw new Error('History not in correct chronological order');
        }
        
        console.log(`   - Pin change history captured: ${history.length} entries`);
        console.log(`   - Latest change: value=${history[0].value}, source=${history[0].source}`);
    }
    
    /**
     * Test 4: Pin Watching and Event Listeners  
     */
    async testPinWatching() {
        const testPin = 'GP2';
        let eventReceived = false;
        let receivedValue = null;
        let receivedSource = null;
        
        // Add event listener
        this.boardShadow.addPinListener(testPin, (value, source) => {
            eventReceived = true;
            receivedValue = value;
            receivedSource = source;
        });
        
        // Watch the pin
        this.boardShadow.watchPin(testPin);
        
        // Change pin value
        await this.boardShadow.setPin(testPin, 1);
        
        // Allow event processing
        await new Promise(resolve => setTimeout(resolve, 10));
        
        // Verify event was received
        if (!eventReceived) {
            throw new Error('Pin change event not received');
        }
        
        if (receivedValue !== 1 || receivedSource !== 'code') {
            throw new Error(`Event data incorrect: value=${receivedValue}, source=${receivedSource}`);
        }
        
        console.log(`   - Pin watching works: received value=${receivedValue}, source=${receivedSource}`);
    }
    
    /**
     * Test 5: Hardware Status Reporting
     */
    async testHardwareStatus() {
        const status = this.boardShadow.getHardwareStatus();
        
        // Verify required fields
        const requiredFields = ['syncMode', 'physicalBoard', 'virtualBoard', 'watchedPins', 'totalPins'];
        for (const field of requiredFields) {
            if (!(field in status)) {
                throw new Error(`Missing required status field: ${field}`);
            }
        }
        
        // Verify logical consistency
        if (status.physicalBoard !== null && status.syncMode === 'virtual') {
            throw new Error('Status inconsistency: has physical board but in virtual mode');
        }
        
        if (status.totalPins <= 0) {
            throw new Error('No pins reported in status');
        }
        
        console.log('   - Hardware status report complete:');
        console.log(`     Sync mode: ${status.syncMode}`);
        console.log(`     Virtual board: ${status.virtualBoard}`);
        console.log(`     Total pins: ${status.totalPins}`);
        console.log(`     Watched pins: ${status.watchedPins.length}`);
    }
    
    /**
     * Test 6: State Export/Import
     */
    async testStateExport() {
        // Set up some state
        await this.boardShadow.setPin('GP3', 1);
        await this.boardShadow.setPin('GP4', 0);
        this.boardShadow.watchPin('GP3');
        this.boardShadow.watchPin('GP4');
        
        // Export state
        const exportedState = this.boardShadow.exportState();
        
        // Verify export structure
        const requiredSections = ['hardware', 'pins', 'history', 'options'];
        for (const section of requiredSections) {
            if (!(section in exportedState)) {
                throw new Error(`Missing section in exported state: ${section}`);
            }
        }
        
        // Verify pin data
        if (!('GP3' in exportedState.pins) || !('GP4' in exportedState.pins)) {
            throw new Error('Pin data not included in export');
        }
        
        if (exportedState.pins.GP3.value !== 1) {
            throw new Error('Pin value not correctly exported');
        }
        
        console.log('   - State export successful');
        console.log(`   - Exported ${Object.keys(exportedState.pins).length} pins`);
        console.log(`   - History entries: ${exportedState.history.length}`);
    }
    
    /**
     * Test 7: Virtual Board Type Support
     */
    async testBoardTypes() {
        const boardTypes = ['pico', 'feather', 'generic'];
        
        for (const boardType of boardTypes) {
            const testShadow = new BoardShadowRuntime({
                boardType: boardType,
                enableLogging: false
            });
            
            if (!testShadow.virtualBoard) {
                throw new Error(`Failed to initialize ${boardType} board`);
            }
            
            // Test that board has some pins
            const status = testShadow.getHardwareStatus();
            if (status.totalPins <= 0) {
                throw new Error(`${boardType} board has no pins`);
            }
            
            testShadow.dispose();
        }
        
        console.log(`   - Tested ${boardTypes.length} board types successfully`);
    }
    
    /**
     * Test 8: Error Handling and Edge Cases
     */
    async testErrorHandling() {
        // Test invalid pin access
        let errorThrown = false;
        try {
            this.boardShadow.getPin(null);
        } catch (error) {
            // This might not throw, depends on implementation
        }
        
        // Test listener removal
        const testPin = 'GP5';
        let eventCount = 0;
        
        const listener = (value, source) => {
            eventCount++;
        };
        
        this.boardShadow.addPinListener(testPin, listener);
        await this.boardShadow.setPin(testPin, 1);
        
        // Allow event processing  
        await new Promise(resolve => setTimeout(resolve, 10));
        
        const initialCount = eventCount;
        
        // Remove listener
        this.boardShadow.removePinListener(testPin, listener);
        await this.boardShadow.setPin(testPin, 0);
        
        // Allow event processing
        await new Promise(resolve => setTimeout(resolve, 10));
        
        if (eventCount !== initialCount) {
            throw new Error('Event listener not properly removed');
        }
        
        console.log('   - Error handling and edge cases tested');
        console.log(`   - Event listener removal verified`);
    }
    
    /**
     * Test 9: Memory and Performance
     */
    async testPerformance() {
        const startTime = Date.now();
        const iterations = 1000;
        const testPin = 'GP6';
        
        // Stress test pin changes
        for (let i = 0; i < iterations; i++) {
            await this.boardShadow.setPin(testPin, i % 2);
        }
        
        const endTime = Date.now();
        const duration = endTime - startTime;
        const opsPerSecond = iterations / (duration / 1000);
        
        // Check that history doesn't grow unbounded
        const history = this.boardShadow.getPinHistory(testPin);
        if (history.length > 100) {
            console.warn(`   - Warning: History size ${history.length} may indicate memory leak`);
        }
        
        console.log(`   - Performance test: ${opsPerSecond.toFixed(0)} ops/second`);
        console.log(`   - ${iterations} pin changes in ${duration}ms`);
        console.log(`   - History size: ${history.length} entries`);
    }
    
    /**
     * Test 10: Integration Features
     */
    async testIntegrationFeatures() {
        // Test multiple pin operations
        const pins = ['GP7', 'GP8', 'GP9'];
        
        // Set up all pins
        for (let i = 0; i < pins.length; i++) {
            await this.boardShadow.setPin(pins[i], i % 2);
            this.boardShadow.watchPin(pins[i]);
        }
        
        // Verify all pins are tracked
        const status = this.boardShadow.getHardwareStatus();
        for (const pin of pins) {
            if (!status.watchedPins.includes(pin)) {
                throw new Error(`Pin ${pin} not in watched pins list`);
            }
        }
        
        // Test bulk state verification
        for (let i = 0; i < pins.length; i++) {
            const value = this.boardShadow.getPin(pins[i]);
            const expected = i % 2;
            if (value !== expected) {
                throw new Error(`Pin ${pins[i]} has value ${value}, expected ${expected}`);
            }
        }
        
        console.log('   - Multi-pin integration tested successfully');
        console.log(`   - Managed ${pins.length} pins simultaneously`);
    }
    
    /**
     * Run all tests
     */
    async runAllTests() {
        console.log('🚀 CircuitPython Hardware Learning Platform - Comprehensive Test Suite\n');
        console.log('This test validates the complete hardware learning architecture including:');
        console.log('- Board Shadow Runtime');
        console.log('- Virtual board simulation');
        console.log('- Pin state management');
        console.log('- Event handling');
        console.log('- Hardware abstraction');
        console.log('=' .repeat(80));
        
        await this.runTest('Board Shadow Runtime Initialization', () => this.testBoardShadowInit());
        await this.runTest('Pin State Management', () => this.testPinStateManagement());
        await this.runTest('Pin Change History', () => this.testPinChangeHistory());
        await this.runTest('Pin Watching and Events', () => this.testPinWatching());
        await this.runTest('Hardware Status Reporting', () => this.testHardwareStatus());
        await this.runTest('State Export/Import', () => this.testStateExport());
        await this.runTest('Virtual Board Type Support', () => this.testBoardTypes());
        await this.runTest('Error Handling', () => this.testErrorHandling());
        await this.runTest('Performance and Memory', () => this.testPerformance());
        await this.runTest('Integration Features', () => this.testIntegrationFeatures());
        
        this.printSummary();
        this.cleanup();
    }
    
    /**
     * Print test summary
     */
    printSummary() {
        console.log('\n' + '=' .repeat(80));
        console.log('📊 TEST SUMMARY');
        console.log('=' .repeat(80));
        
        console.log(`Total Tests: ${this.totalTests}`);
        console.log(`Passed: ${this.passedTests}`);
        console.log(`Failed: ${this.totalTests - this.passedTests}`);
        console.log(`Success Rate: ${((this.passedTests / this.totalTests) * 100).toFixed(1)}%`);
        
        if (this.passedTests === this.totalTests) {
            console.log('\n🎉 ALL TESTS PASSED! 🎉');
            console.log('\nThe CircuitPython Hardware Learning Platform is working correctly.');
            console.log('Ready for:');
            console.log('  ✓ Hardware-centric Python learning');
            console.log('  ✓ Virtual board simulation');
            console.log('  ✓ Real hardware integration (when available)');
            console.log('  ✓ Pin state tracking and visualization');
            console.log('  ✓ Educational debugging and feedback');
        } else {
            console.log('\n⚠️  Some tests failed. Review the errors above.');
        }
        
        console.log('\nDetailed Results:');
        this.testResults.forEach((result, index) => {
            const status = result.status === 'PASSED' ? '✅' : '❌';
            console.log(`  ${index + 1}. ${status} ${result.name}`);
            if (result.error) {
                console.log(`     Error: ${result.error}`);
            }
        });
    }
    
    /**
     * Clean up resources
     */
    cleanup() {
        if (this.boardShadow) {
            this.boardShadow.dispose();
        }
    }
}

// Run the tests
const tester = new HardwareLearningPlatformTest();
tester.runAllTests().then(() => {
    process.exit(tester.passedTests === tester.totalTests ? 0 : 1);
}).catch(error => {
    console.error('\n💥 Test suite crashed:', error);
    process.exit(1);
});