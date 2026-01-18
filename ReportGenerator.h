#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>
#include "TradingStrategy.h"
#include "Backtester.h"

struct AnalysisResult {
    std::string symbol;
    float price;
    float sentiment;
    std::string action;
    float confidence;
    std::string reason;
    BacktestResult backtest;
    Signal signal;
    std::vector<Candle> history; // For charting
};

class ReportGenerator {
public:
    static void generateCSV(const std::vector<AnalysisResult>& results, const std::string& filename = "report.csv") {
        std::ofstream file(filename);
        file << "Symbol,Price,Action,Confidence,Reason,Sentiment,Backtest_Return,Backtest_Sharpe,Option_Type,Option_Strike\n";
        
        for (const auto& r : results) {
            file << r.symbol << ","
                 << r.price << ","
                 << r.action << ","
                 << r.confidence << "%,"
                 << "\"" << r.reason << "\","
                 << r.sentiment << ","
                 << (r.backtest.totalReturn * 100.0f) << "%,"
                 << r.backtest.sharpeRatio << ",";
            
            if (r.signal.option) {
                file << r.signal.option->type << "," << r.signal.option->strike;
            } else {
                file << ",";
            }
            file << "\n";
        }
    }

    static void generateHTML(const std::vector<AnalysisResult>& results, const std::string& filename = "report.html") {
        std::ofstream file(filename);
        
        file << R"(<!DOCTYPE html>
<html>
<head>
    <title>Trading Bot Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { font-family: sans-serif; background: #f4f4f9; padding: 20px; }
        .card { background: white; padding: 15px; margin-bottom: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .buy { border-left: 5px solid #2ecc71; }
        .sell { border-left: 5px solid #e74c3c; }
        .hold { border-left: 5px solid #95a5a6; }
        h1 { color: #2c3e50; }
        table { width: 100%; border-collapse: collapse; margin-top: 10px; }
        th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
    </style>
</head>
<body>
    <h1>Trading Bot Analysis Report</h1>
    <p>Generated on: )" << __DATE__ << " " << __TIME__ << R"(</p>
    <p><i>Disclaimer: Not financial advice. For educational purposes only.</i></p>
)";

        for (const auto& r : results) {
            std::string cls = (r.action == "buy") ? "buy" : ((r.action == "sell") ? "sell" : "hold");
            
            file << "<div class='card " << cls << "'>\n";
            file << "<h2>" << r.symbol << " - " << r.action << " (" << std::fixed << std::setprecision(1) << r.confidence << "%)</h2>\n";
            file << "<table><tr>";
            file << "<td>Price: <b>$" << r.price << "</b></td>";
            file << "<td>Sentiment: <b>" << r.sentiment << "</b></td>";
            file << "<td>Backtest Return: <b>" << (r.backtest.totalReturn * 100.0f) << "%</b></td>";
            file << "<td>Sharpe: <b>" << r.backtest.sharpeRatio << "</b></td>";
            file << "</tr></table>\n";
            
            file << "<p><b>Reason:</b> " << r.reason << "</p>\n";
            
            if (r.action == "hold") {
                 file << "<p><b>Watch Levels:</b> Buy @ $" << r.signal.prospectiveBuy << " | Sell @ $" << r.signal.prospectiveSell << "</p>\n";
            } else if (!r.signal.targets.empty()) {
                 file << "<p><b>Targets:</b> ";
                 for (size_t i = 0; i < r.signal.targets.size(); ++i) {
                     file << "$" << r.signal.targets[i] << (i < r.signal.targets.size()-1 ? ", " : "");
                 }
                 file << "</p>\n";
            }

            if (r.signal.option) {
                file << "<p><b>Option Idea:</b> " << r.signal.option->type << " $" << r.signal.option->strike << " (Exp " << r.signal.option->period_days << "d)</p>\n";
            }

            // Simple Chart Container
            file << "<canvas id='chart_" << r.symbol << "' height='100'></canvas>\n";
            file << "<script>\n";
            file << "new Chart(document.getElementById('chart_" << r.symbol << "'), {\n";
            file << "    type: 'line',\n";
            file << "    data: {\n";
            file << "        labels: [";
            for(size_t i = 0; i < r.history.size(); i+=5) file << i << ","; 
            file << "]\n";
            file << "        datasets:[{\n";
            file << "            label: 'Close Price',\n";
            file << "            data: [";
            for(size_t i = 0; i < r.history.size(); i+=5) file << r.history[i].close << ",";
            file << "]\n";
            file << "            borderColor: '#3498db',\n";
            file << "            tension: 0.1\n";
            file << "        }]\n";
            file << "    }\n";
            file << "});\n";
            file << "</script>\n";
            
            file << "</div>\n";
        }

        file << "</body></html>";
    }
};