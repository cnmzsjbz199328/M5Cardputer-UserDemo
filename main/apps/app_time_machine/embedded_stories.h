#pragma once

struct EmbeddedStoryFile {
    const char* folder;
    const char* json;
};

static const EmbeddedStoryFile kEmbeddedStoryFiles[] = {
    {
        "apple",
R"story_json({
  "title": "Apple",
  "symbol": "AAPL",
  "initialInvestment": 1000,
  "anchors": [
    { "year": 2010.0, "price": 7.00, "label": "'10" },
    { "year": 2012.0, "price": 20.00, "label": "'12" },
    { "year": 2013.0, "price": 17.00, "label": "'13" },
    { "year": 2015.0, "price": 29.00, "label": "'15" },
    { "year": 2018.0, "price": 39.00, "label": "'18" },
    { "year": 2019.5, "price": 73.00, "label": "'19" },
    { "year": 2020.5, "price": 132.00, "label": "'20" },
    { "year": 2021.5, "price": 178.00, "label": "'21" },
    { "year": 2022.5, "price": 130.00, "label": "'22" },
    { "year": 2023.5, "price": 192.00, "label": "'23" },
    { "year": 2024.5, "price": 250.00, "label": "'24" },
    { "year": 2025.5, "price": 230.00, "label": "'25" },
    { "year": 2026.62, "price": 245.00, "label": "now" }
  ],
  "events": [
    { "year": 2012.0, "label": "iPhone era", "mood": "hopeful" },
    { "year": 2020.5, "label": "split", "mood": "euphoria" },
    { "year": 2022.5, "label": "rate scare", "mood": "nervous" },
    { "year": 2024.5, "label": "new highs", "mood": "diamond hands" }
  ]
}
)story_json"
    },
    {
        "bitcoin",
R"story_json({
  "title": "Bitcoin",
  "symbol": "BTC",
  "initialInvestment": 1000,
  "anchors": [
    { "year": 2010.0, "price": 0.30, "label": "'10" },
    { "year": 2011.5, "price": 5.00, "label": "'11" },
    { "year": 2012.5, "price": 13.00, "label": "'12" },
    { "year": 2013.9, "price": 770.00, "label": "'13" },
    { "year": 2014.9, "price": 315.00, "label": "'14" },
    { "year": 2015.9, "price": 430.00, "label": "'15" },
    { "year": 2016.9, "price": 960.00, "label": "'16" },
    { "year": 2017.9, "price": 13800.00, "label": "'17" },
    { "year": 2018.9, "price": 3700.00, "label": "'18" },
    { "year": 2019.9, "price": 7200.00, "label": "'19" },
    { "year": 2020.9, "price": 29000.00, "label": "'20" },
    { "year": 2021.9, "price": 47000.00, "label": "'21" },
    { "year": 2022.9, "price": 16500.00, "label": "'22" },
    { "year": 2023.9, "price": 42000.00, "label": "'23" },
    { "year": 2024.9, "price": 100000.00, "label": "'24" },
    { "year": 2025.7, "price": 118000.00, "label": "'25" },
    { "year": 2026.62, "price": 105000.00, "label": "now" }
  ],
  "events": [
    { "year": 2013.9, "label": "first mania", "mood": "euphoria" },
    { "year": 2017.9, "label": "bubble", "mood": "euphoria" },
    { "year": 2018.9, "label": "winter", "mood": "panic" },
    { "year": 2021.9, "label": "ATH", "mood": "diamond hands" },
    { "year": 2022.9, "label": "crash", "mood": "panic" },
    { "year": 2024.9, "label": "ETF wave", "mood": "euphoria" }
  ]
}
)story_json"
    },
    {
        "gamestop",
R"story_json({
  "title": "GameStop",
  "symbol": "GME",
  "initialInvestment": 1000,
  "anchors": [
    { "year": 2010.0, "price": 5.00, "label": "'10" },
    { "year": 2013.0, "price": 12.00, "label": "'13" },
    { "year": 2016.0, "price": 8.00, "label": "'16" },
    { "year": 2019.0, "price": 1.00, "label": "'19" },
    { "year": 2020.6, "price": 4.70, "label": "'20" },
    { "year": 2021.08, "price": 86.00, "label": "'21" },
    { "year": 2021.5, "price": 40.00, "label": "'21" },
    { "year": 2022.5, "price": 25.00, "label": "'22" },
    { "year": 2023.5, "price": 16.00, "label": "'23" },
    { "year": 2024.4, "price": 48.00, "label": "'24" },
    { "year": 2025.5, "price": 25.00, "label": "'25" },
    { "year": 2026.62, "price": 22.00, "label": "now" }
  ],
  "events": [
    { "year": 2019.0, "label": "left for dead", "mood": "regret" },
    { "year": 2021.08, "label": "squeeze", "mood": "euphoria" },
    { "year": 2021.5, "label": "halt city", "mood": "panic" },
    { "year": 2024.4, "label": "meme wave", "mood": "euphoria" }
  ]
}
)story_json"
    },
    {
        "nvidia",
R"story_json({
  "title": "Nvidia",
  "symbol": "NVDA",
  "initialInvestment": 1000,
  "anchors": [
    { "year": 2010.0, "price": 0.35, "label": "'10" },
    { "year": 2012.0, "price": 0.30, "label": "'12" },
    { "year": 2014.0, "price": 0.48, "label": "'14" },
    { "year": 2016.0, "price": 2.60, "label": "'16" },
    { "year": 2018.0, "price": 3.50, "label": "'18" },
    { "year": 2020.0, "price": 13.00, "label": "'20" },
    { "year": 2021.0, "price": 29.00, "label": "'21" },
    { "year": 2022.5, "price": 14.60, "label": "'22" },
    { "year": 2023.5, "price": 49.00, "label": "'23" },
    { "year": 2024.5, "price": 120.00, "label": "'24" },
    { "year": 2025.5, "price": 180.00, "label": "'25" },
    { "year": 2026.62, "price": 165.00, "label": "now" }
  ],
  "events": [
    { "year": 2016.0, "label": "GPU run", "mood": "hopeful" },
    { "year": 2020.0, "label": "data center", "mood": "euphoria" },
    { "year": 2022.5, "label": "reset", "mood": "nervous" },
    { "year": 2023.5, "label": "AI boom", "mood": "euphoria" },
    { "year": 2024.5, "label": "split", "mood": "diamond hands" }
  ]
}
)story_json"
    },
    {
        "tesla",
R"story_json({
  "title": "Tesla",
  "symbol": "TSLA",
  "initialInvestment": 1000,
  "anchors": [
    { "year": 2010.0, "price": 1.13, "label": "'10 IPO" },
    { "year": 2010.5, "price": 1.78, "label": "'10" },
    { "year": 2011.5, "price": 1.91, "label": "'11" },
    { "year": 2012.5, "price": 2.27, "label": "'12" },
    { "year": 2013.5, "price": 10.07, "label": "'13" },
    { "year": 2014.5, "price": 14.88, "label": "'14" },
    { "year": 2015.5, "price": 16.05, "label": "'15" },
    { "year": 2016.5, "price": 14.29, "label": "'16" },
    { "year": 2017.5, "price": 20.82, "label": "'17" },
    { "year": 2018.5, "price": 22.25, "label": "'18" },
    { "year": 2019.5, "price": 27.97, "label": "'19" },
    { "year": 2020.5, "price": 235.90, "label": "'20" },
    { "year": 2021.5, "price": 353.36, "label": "'21" },
    { "year": 2022.5, "price": 123.68, "label": "'22" },
    { "year": 2023.5, "price": 249.46, "label": "'23" },
    { "year": 2024.5, "price": 405.37, "label": "'24" },
    { "year": 2025.5, "price": 449.72, "label": "'25" },
    { "year": 2026.62, "price": 342.27, "label": "now" }
  ],
  "events": [
    { "year": 2010.0, "label": "IPO", "mood": "hopeful" },
    { "year": 2020.6, "label": "split", "mood": "euphoria" },
    { "year": 2021.5, "label": "mania", "mood": "euphoria" },
    { "year": 2022.5, "label": "drop", "mood": "panic" },
    { "year": 2024.5, "label": "rip", "mood": "diamond hands" }
  ]
}
)story_json"
    },
};