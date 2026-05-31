class Subcli < Formula
  desc "Subscription to proxy client config tool"
  homepage "https://github.com/subcli/subcli"
  url "https://github.com/subcli/subcli/archive/refs/tags/v0.3.0.tar.gz"
  sha256 "replace-with-release-sha256"
  license "MIT"

  depends_on "cmake" => :build

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-DCMAKE_INSTALL_PREFIX=#{prefix}",
           "-DCMAKE_INSTALL_SYSCONFDIR=#{etc}",
           "-DSUBCLI_PORTABLE=OFF",
           *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  def caveats
    <<~EOS
      Initialize a user config if #{etc}/subcli/config.yaml is not used:
        subcli config init --user

      Remove downloaded assets and runtime data before uninstalling if desired:
        subcli purge --all --yes
    EOS
  end

  test do
    system "#{bin}/subcli", "--help"
  end
end
