#pragma once

#include <Core/Types.hpp>
#include <Core/FxMemPool.hpp>

#include <vector>
#include <string>

#include <cstring>

class FxTokenizer
{
private:
public:
	const char* SingleCharOperators = "=(){}+.,;";

	struct Token
	{
		char* Start = nullptr;
		char* End = nullptr;

		uint32 Length = 0;

		void Increment()
		{
			if (Length > 20) {

			}
			Length++;
		}

		inline std::string GetStr() const
		{
			return std::string(Start, Length);
		}

		inline char* GetHeapStr() const
		{
			char* str = FxMemPool::Alloc<char>(Length + 1);

			std::strncpy(str, Start, Length);
			str[Length] = 0;

			return str;
		}

		int64 ToInt() const
		{
			char buffer[32];
			std::strncpy(buffer, Start, Length);
			buffer[Length] = 0;

			char* end = nullptr;
			return strtoll(buffer, &end, 10);
		}

		bool IsEmpty() const
		{
			return (Start == nullptr || Length == 0);
		}

		void Clear()
		{
			Start = nullptr;
			End = nullptr;
			Length = 0;
		}
	};

	FxTokenizer() = delete;

	FxTokenizer(char* data, uint32 buffer_size)
		: mData(data), mDataEnd(data + buffer_size)
	{

	}

	void SubmitTokenIfData(Token& token, char* end_ptr = nullptr, char* start_ptr = nullptr)
	{
		if (token.IsEmpty()) {
			return;
		}

		if (end_ptr == nullptr) {
			end_ptr = mData;
		}

		if (start_ptr == nullptr) {
			start_ptr = mData;
		}

		token.End = mData;

		mTokens.emplace_back(token);
		token.Clear();

		token.Start = mData;
	}

	bool CheckOperators(Token& current_token, char ch)
	{
		bool is_operator = (strchr(SingleCharOperators, ch) != NULL);

		if (is_operator) {
			// If there is data waiting, submit to the token list
			SubmitTokenIfData(current_token, mData);

			// Submit the operator as its own token
			current_token.Increment();

			char* new_data = mData + 1;
			SubmitTokenIfData(current_token, mData, new_data);
			mData++;

			return true;
		}

		return false;
	}

	void Tokenize()
	{
		Token current_token;
		current_token.Start = mData;

		char ch;
		while (mData < mDataEnd && ((ch = *(mData)))) {

			if (ch == '"') {
				// If we are not currently in a string, submit the token if there is data waiting
				if (!mInString) {
					SubmitTokenIfData(current_token);
				}

				mInString = !mInString;
			}

			if (mInString) {
				mData++;
				current_token.Increment();
				continue;
			}

			if (IsWhitespace(ch)) {
				SubmitTokenIfData(current_token);

				mData++;
				current_token.Start = mData;
				continue;
			}

			if (CheckOperators(current_token, ch)) {
				continue;
			}

			mData++;
			current_token.Increment();
		}
		SubmitTokenIfData(current_token);
	}

	std::vector<Token>& GetTokens()
	{
		return mTokens;
	}

private:
	bool IsWhitespace(char ch)
	{
		return (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
	}

private:
	char* mData = nullptr;
	char* mDataEnd = nullptr;

	bool mInString = false;

	std::vector<Token> mTokens;
};
