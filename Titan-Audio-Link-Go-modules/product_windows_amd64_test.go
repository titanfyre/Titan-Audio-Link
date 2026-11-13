package titan_audio_link

import (
	"encoding/json"
	"os"
	"reflect"
	"testing"
)

func TestProduct(t *testing.T) {
	path := os.Getenv("SDK_TEST_FIXTURE")
	c, err := NewWithLibrary(path)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if _, err = c.Call("check", nil); err == nil {
		t.Fatal("invalid request accepted")
	}
	if path != "" {
		data, err := os.ReadFile("docs/contract.json")
		if err != nil {
			t.Fatal(err)
		}
		var tests []map[string]any
		if err = json.Unmarshal(data, &tests); err != nil {
			t.Fatal(err)
		}
		for _, test := range tests {
			raw, err := c.Call(test["operation"].(string), test["arguments"])
			if err != nil {
				t.Fatal(err)
			}
			var result map[string]any
			if err = json.Unmarshal(raw, &result); err != nil {
				t.Fatal(err)
			}
			value := result[test["expected_key"].(string)]
			if expected, ok := test["expected_value"]; ok && !reflect.DeepEqual(value, expected) {
				t.Fatalf("wrong result: %s", raw)
			}
			if minimum, ok := test["minimum_length"]; ok && len(value.([]any)) < int(minimum.(float64)) {
				t.Fatal("array missing")
			}
		}
	}
}
