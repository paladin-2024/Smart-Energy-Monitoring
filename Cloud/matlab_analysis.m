% =============================================================================
% FILE:        matlab_analysis.m
% PROJECT:     Smart Energy Monitoring System — Part B (ThingSpeak MATLAB)
% DESCRIPTION: Reads power readings from ThingSpeak Field 3, computes a
%              moving average to smooth out noise, and plots the raw power
%              trend alongside the smoothed overlay and the 150W overload
%              threshold line. Run this script in ThingSpeak MATLAB Analysis.
% AUTHOR:      Caleb
% DATE:        2026-04-17
% VERSION:     1.0.0
% LICENSE:     MIT
% =============================================================================

% ---------------------------------------------------------------------------
% CONFIGURATION — update CHANNEL_ID and READ_API_KEY to match your channel
% ---------------------------------------------------------------------------

% Your ThingSpeak channel ID — found on your channel page (numeric, e.g. 2345678)
CHANNEL_ID = YOUR_CHANNEL_ID;

% Your ThingSpeak Read API Key — found in channel → API Keys tab
READ_API_KEY = 'YOUR_READ_API_KEY';

% Number of data points to fetch and plot
% 100 points = approximately 25 minutes of data at 15-second intervals
NUM_POINTS = 100;

% Moving average window size — number of readings to average together.
% A larger window produces a smoother line but lags further behind real values.
% A smaller window tracks changes faster but retains more noise.
% 10 readings = 2.5 minutes of data — good balance for this sample rate.
WINDOW_SIZE = 10;

% Overload threshold in Watts — must match POWER_THRESHOLD in sketch_wifi.ino
THRESHOLD_W = 150;

% ---------------------------------------------------------------------------
% STEP 1: Fetch power data from ThingSpeak (Field 3 = Power in Watts)
% thingSpeakRead() returns a numeric array of the most recent NUM_POINTS values
% from the specified field. Returns NaN for any missing entries.
% ---------------------------------------------------------------------------
rawPower = thingSpeakRead(CHANNEL_ID, ...
    'Fields', 3, ...           % Field 3 is Power (W)
    'NumPoints', NUM_POINTS, ...
    'ReadKey', READ_API_KEY);

% Remove any NaN values that occur when data is missing or the channel is new
% isnan() returns a logical array; ~ inverts it to keep only valid readings
rawPower = rawPower(~isnan(rawPower));

% Guard: if fewer than 2 readings exist, we cannot plot anything meaningful
if length(rawPower) < 2
    disp('Not enough data to plot. Wait for more readings to arrive.');
    return;
end

% ---------------------------------------------------------------------------
% STEP 2: Compute moving average
% movmean(X, K) computes the K-point centred moving average of X.
% The 'Endpoints' option controls how boundaries are handled:
%   'shrink' — at the edges where fewer than K points exist, the window
%              shrinks to use only available data rather than padding with zeros.
%              This prevents the smoothed line from dropping to zero at the ends.
% ---------------------------------------------------------------------------
smoothedPower = movmean(rawPower, WINDOW_SIZE, 'Endpoints', 'shrink');

% ---------------------------------------------------------------------------
% STEP 3: Build x-axis (reading number)
% We use sequential reading numbers rather than timestamps because ThingSpeak
% MATLAB Analysis does not always provide a reliable time vector.
% ---------------------------------------------------------------------------
x = 1:length(rawPower); % [1, 2, 3, ..., N]

% ---------------------------------------------------------------------------
% STEP 4: Plot
% Three elements on the same axes:
%   1. Raw power readings (blue line, semi-transparent)
%   2. Moving average overlay (red line, bold — the key insight)
%   3. Overload threshold (black dashed — reference line)
% ---------------------------------------------------------------------------
figure('Color', 'white');

% Raw power — thin blue line, slightly transparent to let the overlay stand out
plot(x, rawPower, ...
    'Color', [0.2, 0.6, 1.0, 0.7], ...  % RGBA: light blue, 70% opacity
    'LineWidth', 1.2, ...
    'DisplayName', 'Raw Power (W)');

hold on;

% Moving average — thicker red line, the main analytical output
plot(x, smoothedPower, ...
    'Color', [0.9, 0.2, 0.2], ...        % RGB: bright red
    'LineWidth', 2.5, ...
    'DisplayName', sprintf('%d-point Moving Avg (W)', WINDOW_SIZE));

% Threshold reference line — dashed black, labelled
yline(THRESHOLD_W, ...
    '--k', ...                            % Dashed black
    'LineWidth', 1.5, ...
    'DisplayName', sprintf('Threshold (%dW)', THRESHOLD_W));

hold off;

% ---------------------------------------------------------------------------
% STEP 5: Format the plot
% ---------------------------------------------------------------------------
xlabel('Reading Number', 'FontSize', 12);
ylabel('Power (Watts)', 'FontSize', 12);
title(sprintf('Energy Consumption — %d-point Moving Average', WINDOW_SIZE), ...
    'FontSize', 14, 'FontWeight', 'bold');

legend('Location', 'northwest', 'FontSize', 10);
grid on;
grid minor; % Minor grid lines help read values between major ticks

% Set y-axis to start at 0 with a small buffer above the max reading
yMax = max([max(rawPower), THRESHOLD_W]) * 1.15; % 15% headroom
ylim([0, yMax]);

% ---------------------------------------------------------------------------
% STEP 6: Annotate overload events
% Find readings that exceeded the threshold and mark them with red circles
% so the viewer can immediately see when overload events occurred.
% ---------------------------------------------------------------------------
overloadIdx = find(rawPower > THRESHOLD_W); % Indices where power > 150W

if ~isempty(overloadIdx)
    hold on;
    % Red circles on the raw power line at overload points
    plot(x(overloadIdx), rawPower(overloadIdx), ...
        'ro', ...                          % Red circles
        'MarkerSize', 8, ...
        'MarkerFaceColor', [1, 0.3, 0.3], ...
        'DisplayName', 'Overload Events');
    hold off;

    fprintf('Overload events detected: %d out of %d readings (%.1f%%)\n', ...
        length(overloadIdx), length(rawPower), ...
        100 * length(overloadIdx) / length(rawPower));
else
    fprintf('No overload events in the last %d readings.\n', length(rawPower));
end

% ---------------------------------------------------------------------------
% STEP 7: Print summary statistics to MATLAB console
% ---------------------------------------------------------------------------
fprintf('\n--- Power Summary (last %d readings) ---\n', length(rawPower));
fprintf('Mean power:    %.2f W\n', mean(rawPower));
fprintf('Max power:     %.2f W\n', max(rawPower));
fprintf('Min power:     %.2f W\n', min(rawPower));
fprintf('Std deviation: %.2f W\n', std(rawPower));
fprintf('Moving avg window: %d readings (%.0f seconds)\n', ...
    WINDOW_SIZE, WINDOW_SIZE * 15);
